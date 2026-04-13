#!/bin/bash
#SBATCH --job-name=condec-benchmark
#SBATCH --partition=q-lxe5wipa
#SBATCH --nodes=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=8G
#SBATCH --time=24:00:00
#SBATCH --output=condec-benchmark_%j.out
#SBATCH --error=condec-benchmark_%j.err

set -euo pipefail

resolve_path() {
    python3 - "$1" <<'PY'
import os
import sys
print(os.path.realpath(sys.argv[1]))
PY
}

SCRIPT_PATH="$(resolve_path "$0")"
PROJECT_ROOT="$(cd "$(dirname "$SCRIPT_PATH")" && pwd)"

# Load local secrets (.env is gitignored; copy from .env.example to configure)
if [ -f "$PROJECT_ROOT/.env" ]; then
    # shellcheck disable=SC1091
    source "$PROJECT_ROOT/.env"
fi

DEFAULT_PARTITION="q-lxe5wipa"
DEFAULT_MAX_PARALLEL="59"
DEFAULT_WORKER_CPUS="1"
DEFAULT_WORKER_MEM="8G"
DEFAULT_COLLECT_CPUS="1"
DEFAULT_COLLECT_MEM="4G"
DEFAULT_COLLECT_TIME="00:10:00"

usage() {
    cat <<'EOF'
Usage:
  Submit the benchmark pipeline:
       bash benchmark_compare.slurm.sh

Optional environment variables before submission:
  export ABC_BIN=/path/to/abc
  export CADICAL_SOLVER_BIN=/path/to/cadical
  export KISSAT_BIN=/path/to/kissat
  export BASELINE_CONDEC_BIN=/path/to/condec_baseline
  export IMPROVED_CONDEC_BIN=/path/to/condec_improved
  export TIMEOUT_SEC=3600
  export PARTITION=q-lxe5wipa
  export MAX_PARALLEL=59   # default: matches benchmark count; lower to throttle
  export WORKER_CPUS=1
  export WORKER_MEM=8G
  export WORKER_TIME=05:30:00
  export COLLECT_CPUS=1
  export COLLECT_MEM=4G
  export COLLECT_TIME=00:10:00
  export RUN_NAME=my-benchmark-run
  export DEBUG_LOGS=1

This script only submits and collects benchmark jobs. It expects all required
binaries to be available before submission.

Default binary locations:
  tools/abc/abc
  tools/kissat/build/kissat
  cadical/build/cadical
  benchmark_artifacts/condec_baseline
  benchmark_artifacts/condec_improved

The generated CSV columns are:
  filename,abc-&cec,cadical,kissat,condec,condec-improved
EOF
}

load_slurm_module() {
    if type module >/dev/null 2>&1; then
        module load slurm/slurm/23.02.7 >/dev/null 2>&1 || true
    fi
}

# Send a Server酱 WeChat notification. Silently skips if SERVERCHAN_TOKEN is unset.
send_serverchan() {
    local title="$1"
    local content="$2"

    if [ -z "${SERVERCHAN_TOKEN:-}" ]; then
        return 0
    fi

    local encoded_title encoded_content
    encoded_title="$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1]))" "$title" 2>/dev/null || printf '%s' "$title")"
    encoded_content="$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1]))" "$content" 2>/dev/null || printf '%s' "$content")"

    curl -s -X POST "https://sctapi.ftqq.com/${SERVERCHAN_TOKEN}.send" \
        -d "title=${encoded_title}&desp=${encoded_content}" \
        > /dev/null 2>&1 || true
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

require_positive_integer() {
    local label="$1"
    local value="$2"

    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "$label must be a positive integer: $value" >&2
        exit 1
    fi
}

require_executable() {
    local label="$1"
    local path="$2"

    if [ -z "$path" ]; then
        echo "Missing required binary path for $label." >&2
        exit 1
    fi

    if [ ! -x "$path" ]; then
        echo "$label is not executable: $path" >&2
        exit 1
    fi
}

seconds_to_slurm_time() {
    local total_seconds="$1"
    local hours=$((total_seconds / 3600))
    local minutes=$(((total_seconds % 3600) / 60))
    local seconds=$((total_seconds % 60))
    printf '%02d:%02d:%02d\n' "$hours" "$minutes" "$seconds"
}

write_config() {
    local config_file="$1"
    : > "$config_file"

    printf 'PROJECT_ROOT=%q\n' "$PROJECT_ROOT" >> "$config_file"
    printf 'RUN_DIR=%q\n' "$RUN_DIR" >> "$config_file"
    printf 'RUN_NAME=%q\n' "$RUN_NAME" >> "$config_file"
    printf 'TIMEOUT_SEC=%q\n' "$TIMEOUT_SEC" >> "$config_file"
    printf 'PARTITION=%q\n' "$PARTITION" >> "$config_file"
    printf 'MAX_PARALLEL=%q\n' "$MAX_PARALLEL" >> "$config_file"
    printf 'WORKER_CPUS=%q\n' "$WORKER_CPUS" >> "$config_file"
    printf 'WORKER_MEM=%q\n' "$WORKER_MEM" >> "$config_file"
    printf 'WORKER_TIME=%q\n' "$WORKER_TIME" >> "$config_file"
    printf 'COLLECT_CPUS=%q\n' "$COLLECT_CPUS" >> "$config_file"
    printf 'COLLECT_MEM=%q\n' "$COLLECT_MEM" >> "$config_file"
    printf 'COLLECT_TIME=%q\n' "$COLLECT_TIME" >> "$config_file"
    printf 'ABC_BIN=%q\n' "$ABC_BIN" >> "$config_file"
    printf 'CADICAL_SOLVER_BIN=%q\n' "$CADICAL_SOLVER_BIN" >> "$config_file"
    printf 'KISSAT_BIN=%q\n' "$KISSAT_BIN" >> "$config_file"
    printf 'BASELINE_CONDEC_BIN=%q\n' "$BASELINE_CONDEC_BIN" >> "$config_file"
    printf 'IMPROVED_CONDEC_BIN=%q\n' "$IMPROVED_CONDEC_BIN" >> "$config_file"
    printf 'BASELINE_COMMIT=%q\n' "${BASELINE_COMMIT:-}" >> "$config_file"
    printf 'CURRENT_REF=%q\n' "${CURRENT_REF:-}" >> "$config_file"
    printf 'DEBUG_LOGS=%q\n' "$DEBUG_LOGS" >> "$config_file"
}

load_config() {
    if [ -z "${RUN_DIR:-}" ]; then
        echo "RUN_DIR is not set." >&2
        exit 1
    fi

    local config_file="$RUN_DIR/config.env"
    if [ ! -f "$config_file" ]; then
        echo "Missing config file: $config_file" >&2
        exit 1
    fi

    # shellcheck disable=SC1090
    source "$config_file"
}

create_manifest() {
    local manifest="$1"
    python3 - "$PROJECT_ROOT/benchmarks/aig" > "$manifest" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
for path in sorted(root.glob("*.aig")):
    print(path.resolve())
PY
}

resolve_binary() {
    local explicit_value="$1"
    shift

    if [ -n "$explicit_value" ]; then
        printf '%s\n' "$explicit_value"
        return
    fi

    local candidate
    for candidate in "$@"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return
        fi
    done

    printf '\n'
}

submit_pipeline() {
    load_slurm_module

    require_cmd sbatch
    require_cmd python3

    TIMEOUT_SEC="${TIMEOUT_SEC:-3600}"
    PARTITION="${PARTITION:-$DEFAULT_PARTITION}"
    MAX_PARALLEL="${MAX_PARALLEL:-$DEFAULT_MAX_PARALLEL}"
    WORKER_CPUS="${WORKER_CPUS:-$DEFAULT_WORKER_CPUS}"
    WORKER_MEM="${WORKER_MEM:-$DEFAULT_WORKER_MEM}"
    COLLECT_CPUS="${COLLECT_CPUS:-$DEFAULT_COLLECT_CPUS}"
    COLLECT_MEM="${COLLECT_MEM:-$DEFAULT_COLLECT_MEM}"
    COLLECT_TIME="${COLLECT_TIME:-$DEFAULT_COLLECT_TIME}"
    RUN_NAME="${RUN_NAME:-benchmark-$(date +%Y%m%d-%H%M%S)}"
    RUN_DIR="${RUN_DIR:-$PROJECT_ROOT/result/$RUN_NAME}"
    DEBUG_LOGS="${DEBUG_LOGS:-0}"
    WORKER_TIME="${WORKER_TIME:-$(seconds_to_slurm_time "$((TIMEOUT_SEC * 5 + 1800))")}"

    require_positive_integer "TIMEOUT_SEC" "$TIMEOUT_SEC"
    require_positive_integer "MAX_PARALLEL" "$MAX_PARALLEL"
    require_positive_integer "WORKER_CPUS" "$WORKER_CPUS"
    require_positive_integer "COLLECT_CPUS" "$COLLECT_CPUS"

    local abc_from_path=""
    local kissat_from_path=""
    if command -v abc >/dev/null 2>&1; then
        abc_from_path="$(command -v abc)"
    fi
    if command -v kissat >/dev/null 2>&1; then
        kissat_from_path="$(command -v kissat)"
    fi

    ABC_BIN="$(resolve_binary "${ABC_BIN:-}" "$PROJECT_ROOT/tools/abc/abc" "$abc_from_path")"
    KISSAT_BIN="$(resolve_binary "${KISSAT_BIN:-}" "$PROJECT_ROOT/tools/kissat/build/kissat" "$kissat_from_path")"
    CADICAL_SOLVER_BIN="${CADICAL_SOLVER_BIN:-$PROJECT_ROOT/cadical/build/cadical}"
    BASELINE_CONDEC_BIN="${BASELINE_CONDEC_BIN:-$PROJECT_ROOT/benchmark_artifacts/condec_baseline}"
    IMPROVED_CONDEC_BIN="${IMPROVED_CONDEC_BIN:-$PROJECT_ROOT/benchmark_artifacts/condec_improved}"

    require_executable "ABC_BIN" "$ABC_BIN"
    require_executable "CADICAL_SOLVER_BIN" "$CADICAL_SOLVER_BIN"
    require_executable "KISSAT_BIN" "$KISSAT_BIN"
    require_executable "BASELINE_CONDEC_BIN" "$BASELINE_CONDEC_BIN"
    require_executable "IMPROVED_CONDEC_BIN" "$IMPROVED_CONDEC_BIN"

    mkdir -p "$PROJECT_ROOT/result"
    mkdir -p "$RUN_DIR"/{rows,slurm,tmp}
    if [ "$DEBUG_LOGS" = "1" ]; then
        mkdir -p "$RUN_DIR/logs"
    fi

    local manifest="$RUN_DIR/benchmarks.txt"
    create_manifest "$manifest"

    local benchmark_count
    benchmark_count="$(python3 - "$manifest" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
lines = [line for line in path.read_text().splitlines() if line.strip()]
print(len(lines))
PY
)"

    if [ "$benchmark_count" -eq 0 ]; then
        echo "No benchmark files were found in benchmarks/aig." >&2
        exit 1
    fi

    write_config "$RUN_DIR/config.env"

    local array_job
    local collect_job
    local array_spec
    local row_output_target
    local row_error_target

    array_spec="0-$((benchmark_count - 1))%${MAX_PARALLEL}"
    row_output_target="/dev/null"
    row_error_target="/dev/null"
    if [ "$DEBUG_LOGS" = "1" ]; then
        row_output_target="$RUN_DIR/slurm/row_%A_%a.out"
        row_error_target="$RUN_DIR/slurm/row_%A_%a.err"
    fi

    array_job="$(
        sbatch \
            --parsable \
            --partition="$PARTITION" \
            --nodes=1 \
            --array="$array_spec" \
            --job-name="${RUN_NAME}-row" \
            --cpus-per-task="$WORKER_CPUS" \
            --mem="$WORKER_MEM" \
            --time="$WORKER_TIME" \
            --output="$row_output_target" \
            --error="$row_error_target" \
            --export=ALL,RUN_DIR="$RUN_DIR" \
            "$SCRIPT_PATH" worker
    )"

    collect_job="$(
        sbatch \
            --parsable \
            --dependency="afterok:${array_job}" \
            --partition="$PARTITION" \
            --nodes=1 \
            --job-name="${RUN_NAME}-collect" \
            --cpus-per-task="$COLLECT_CPUS" \
            --mem="$COLLECT_MEM" \
            --time="$COLLECT_TIME" \
            --output="$RUN_DIR/slurm/collect_%j.out" \
            --error="$RUN_DIR/slurm/collect_%j.err" \
            --export=ALL,RUN_DIR="$RUN_DIR" \
            "$SCRIPT_PATH" collect
    )"

    cat <<EOF
Submitted benchmark pipeline.
  Run directory      : $RUN_DIR
  Benchmarks         : $benchmark_count
  Partition          : $PARTITION
  Array spec         : $array_spec
  Array job          : $array_job
  Collect job        : $collect_job
  Per-tool timeout   : ${TIMEOUT_SEC}s
  Worker CPUs/task   : $WORKER_CPUS
  Worker mem/task    : $WORKER_MEM
  Worker walltime    : $WORKER_TIME
  Collect CPUs/task  : $COLLECT_CPUS
  Collect mem/task   : $COLLECT_MEM
  Collect walltime   : $COLLECT_TIME
  ABC binary         : $ABC_BIN
  CaDiCaL binary     : $CADICAL_SOLVER_BIN
  Kissat binary      : $KISSAT_BIN
  Baseline CondEC    : $BASELINE_CONDEC_BIN
  Improved CondEC    : $IMPROVED_CONDEC_BIN
EOF

    if [ -n "${BASELINE_COMMIT:-}" ]; then
        echo "  Baseline commit   : $BASELINE_COMMIT"
    fi
    if [ -n "${CURRENT_REF:-}" ]; then
        echo "  Improved ref      : $CURRENT_REF"
    fi

    cat <<EOF

Follow progress with:
  squeue -j $array_job,$collect_job

Final CSV will be written to:
  $RUN_DIR/CondEC_result_compare.csv
  $PROJECT_ROOT/result/CondEC_result_compare.csv
EOF

    send_serverchan \
        "CondEC Benchmark Submitted" \
        "**Run**: ${RUN_NAME}
**Benchmarks**: ${benchmark_count}
**Array job**: ${array_job}  |  **Collect job**: ${collect_job}
**Per-tool timeout**: ${TIMEOUT_SEC}s
**Max walltime**: ${WORKER_TIME}
You will receive another notification when results are ready."
}

read_manifest_line() {
    local manifest="$1"
    local index="$2"

    python3 - "$manifest" "$index" <<'PY'
from pathlib import Path
import sys

manifest = Path(sys.argv[1])
index = int(sys.argv[2])
lines = [line.strip() for line in manifest.read_text().splitlines() if line.strip()]
if index < 0 or index >= len(lines):
    raise SystemExit(f"Manifest index out of range: {index}")
print(lines[index])
PY
}

finalize_summary_value() {
    local time_value="$1"
    local status="$2"
    local timeout_value="$3"

    if [ -n "$time_value" ]; then
        printf '%s\n' "$time_value"
        return
    fi

    if [ "$status" = "TIMEOUT" ]; then
        printf '%s\n' "$timeout_value"
        return
    fi

    printf '%s\n' "$status"
}

make_temp_file() {
    local tag="$1"
    local suffix="$2"
    mktemp "$RUN_DIR/tmp/${tag}.${suffix}.XXXXXX"
}

cleanup_temp_files() {
    rm -f "$@"
}

run_abc_cec() {
    local aig_with_outputs="$1"
    local tag="$2"
    local stdout_file
    stdout_file="$(make_temp_file "$tag" "abc")"
    trap 'cleanup_temp_files "$stdout_file"' RETURN

    local status="UNKNOWN"
    local time_value=""

    if [ ! -f "$aig_with_outputs" ]; then
        printf 'MISSING_INPUT\n'
        return
    fi

    if timeout "$TIMEOUT_SEC" "$ABC_BIN" -c "&r $aig_with_outputs; &cec -m; quit" >"$stdout_file" 2>&1; then
        :
    else
        local exit_code=$?
        if [ "$exit_code" -eq 124 ]; then
            printf '%s\n' "$TIMEOUT_SEC"
            return
        fi
    fi

    if grep -q "Networks are equivalent" "$stdout_file"; then
        status="UNSAT"
    elif grep -q "Networks are NOT EQUIVALENT" "$stdout_file"; then
        status="SAT"
    fi

    time_value="$(
        sed -nE 's/.*Time = *([0-9]+(\.[0-9]+)?).*/\1/p' "$stdout_file" | tail -n1
    )"

    if [ "${DEBUG_LOGS:-0}" = "1" ]; then
        cp "$stdout_file" "$RUN_DIR/logs/${tag}.abc.log"
    fi

    finalize_summary_value "$time_value" "$status" "$TIMEOUT_SEC"
}

run_sat_solver() {
    local solver_bin="$1"
    local cnf_file="$2"
    local tag="$3"
    local stdout_file
    local time_file
    stdout_file="$(make_temp_file "$tag" "solver")"
    time_file="$(make_temp_file "$tag" "time")"
    trap 'cleanup_temp_files "$stdout_file" "$time_file"' RETURN

    local status="UNKNOWN"
    local time_value=""

    if [ ! -f "$cnf_file" ]; then
        printf 'MISSING_INPUT\n'
        return
    fi

    if timeout "$TIMEOUT_SEC" /usr/bin/time -f "%e" -o "$time_file" -- "$solver_bin" "$cnf_file" -q >"$stdout_file" 2>&1; then
        :
    else
        local exit_code=$?
        if [ "$exit_code" -eq 124 ]; then
            printf '%s\n' "$TIMEOUT_SEC"
            return
        fi
    fi

    if grep -q "^s[[:space:]]\+UNSATISFIABLE" "$stdout_file"; then
        status="UNSATISFIABLE"
    elif grep -q "^s[[:space:]]\+SATISFIABLE" "$stdout_file"; then
        status="SATISFIABLE"
    fi

    if [ -f "$time_file" ]; then
        time_value="$(grep -oE '^[0-9]+(\.[0-9]+)?$' "$time_file" || true)"
    fi

    if [ "${DEBUG_LOGS:-0}" = "1" ]; then
        cp "$stdout_file" "$RUN_DIR/logs/${tag}.solver.log"
        cp "$time_file" "$RUN_DIR/logs/${tag}.time.log"
    fi

    finalize_summary_value "$time_value" "$status" "$TIMEOUT_SEC"
}

run_condec() {
    local condec_bin="$1"
    local aig_file="$2"
    local tag="$3"
    local stdout_file
    stdout_file="$(make_temp_file "$tag" "condec")"
    trap 'cleanup_temp_files "$stdout_file"' RETURN

    local output=""
    local status="PARSE_ERROR"
    local time_value=""

    if [ ! -f "$aig_file" ]; then
        printf 'MISSING_INPUT\n'
        return
    fi

    if timeout "$TIMEOUT_SEC" "$condec_bin" "$aig_file" -q >"$stdout_file" 2>&1; then
        :
    else
        local exit_code=$?
        if [ "$exit_code" -eq 124 ]; then
            printf '%s\n' "$TIMEOUT_SEC"
            return
        fi
        printf 'ERROR_%s\n' "$exit_code"
        return
    fi

    output="$(tr '\n' ' ' < "$stdout_file")"
    if [[ "$output" =~ ,[[:space:]]*([A-Z]+),[[:space:]]*time:[[:space:]]*([0-9]+(\.[0-9]+)?)[[:space:]]*s ]]; then
        status="${BASH_REMATCH[1]}"
        time_value="${BASH_REMATCH[2]}"
    fi

    if [ "${DEBUG_LOGS:-0}" = "1" ]; then
        cp "$stdout_file" "$RUN_DIR/logs/${tag}.condec.log"
    fi

    finalize_summary_value "$time_value" "$status" "$TIMEOUT_SEC"
}

run_worker() {
    load_config

    require_cmd python3
    require_cmd timeout
    require_cmd /usr/bin/time
    require_executable "ABC_BIN" "$ABC_BIN"
    require_executable "CADICAL_SOLVER_BIN" "$CADICAL_SOLVER_BIN"
    require_executable "KISSAT_BIN" "$KISSAT_BIN"
    require_executable "BASELINE_CONDEC_BIN" "$BASELINE_CONDEC_BIN"
    require_executable "IMPROVED_CONDEC_BIN" "$IMPROVED_CONDEC_BIN"

    local manifest="$RUN_DIR/benchmarks.txt"
    local task_index="${SLURM_ARRAY_TASK_ID:-}"

    if [ -z "$task_index" ]; then
        echo "SLURM_ARRAY_TASK_ID is not set." >&2
        exit 1
    fi

    local aig_file
    aig_file="$(read_manifest_line "$manifest" "$task_index")"

    local filename
    filename="$(basename "$aig_file")"
    local stem="${filename%.aig}"
    local abc_file="$PROJECT_ROOT/benchmarks/aig-and-output/${stem}_move_and.aig"
    local cnf_file="$PROJECT_ROOT/benchmarks/cnf/${stem}.cnf"

    local abc_value
    local cadical_value
    local kissat_value
    local baseline_value
    local improved_value

    abc_value="$(run_abc_cec "$abc_file" "${task_index}_${stem}")"
    cadical_value="$(run_sat_solver "$CADICAL_SOLVER_BIN" "$cnf_file" "${task_index}_${stem}_cadical")"
    kissat_value="$(run_sat_solver "$KISSAT_BIN" "$cnf_file" "${task_index}_${stem}_kissat")"
    baseline_value="$(run_condec "$BASELINE_CONDEC_BIN" "$aig_file" "${task_index}_${stem}_baseline")"
    improved_value="$(run_condec "$IMPROVED_CONDEC_BIN" "$aig_file" "${task_index}_${stem}_improved")"

    printf '%s,%s,%s,%s,%s,%s\n' \
        "$filename" \
        "$abc_value" \
        "$cadical_value" \
        "$kissat_value" \
        "$baseline_value" \
        "$improved_value" \
        > "$RUN_DIR/rows/$(printf '%03d' "$task_index").csv"
}

collect_results() {
    load_config

    local merged_csv="$RUN_DIR/CondEC_result_compare.csv"
    local published_csv="$PROJECT_ROOT/result/CondEC_result_compare.csv"

    {
        echo "filename,abc-&cec,cadical,kissat,condec,condec-improved"
        python3 - "$RUN_DIR/rows" <<'PY'
from pathlib import Path
import sys

rows_dir = Path(sys.argv[1])
for row_file in sorted(rows_dir.glob("*.csv")):
    text = row_file.read_text().strip()
    if text:
        print(text)
PY
    } > "$merged_csv"

    cp "$merged_csv" "$published_csv"

    cat <<EOF
Benchmark collection finished.
  Merged CSV : $merged_csv
  Published  : $published_csv
EOF

    local notify_body
    notify_body="$(python3 - "$merged_csv" <<'PY'
from pathlib import Path
import sys

csv_path = Path(sys.argv[1])
lines = [l for l in csv_path.read_text().splitlines() if l.strip()]
rows = lines[1:]  # skip header

total = len(rows)
improved_faster = 0
speedup_ratios = []

for row in rows:
    parts = row.split(',')
    if len(parts) < 6:
        continue
    try:
        baseline = float(parts[4])
        improved = float(parts[5])
        if baseline > 0 and improved > 0:
            ratio = baseline / improved
            speedup_ratios.append(ratio)
            if improved < baseline:
                improved_faster += 1
    except ValueError:
        pass

if speedup_ratios:
    avg_speedup = sum(speedup_ratios) / len(speedup_ratios)
    max_speedup = max(speedup_ratios)
    print(f"**Benchmarks**: {total}")
    print(f"**Improved faster**: {improved_faster}/{len(speedup_ratios)}")
    print(f"**Avg speedup**: {avg_speedup:.2f}x")
    print(f"**Max speedup**: {max_speedup:.2f}x")
else:
    print(f"**Benchmarks**: {total} (no numeric condec results to compare)")
PY
)"

    send_serverchan \
        "CondEC Benchmark Complete" \
        "${notify_body}
**CSV**: ${published_csv}"
}

MODE="${1:-submit}"

case "$MODE" in
    submit)
        submit_pipeline
        ;;
    worker)
        run_worker
        ;;
    collect)
        collect_results
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        echo "Unknown mode: $MODE" >&2
        usage >&2
        exit 1
        ;;
esac
