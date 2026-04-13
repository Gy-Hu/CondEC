#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
ABC_BIN="${ABC_BIN:-$PROJECT_ROOT/tools/abc/abc}"
OUTPUT_CSV="$PROJECT_ROOT/results_abc_cec.csv"
TIMEOUT_SEC="${TIMEOUT_SEC:-3600}"

if ! command -v timeout >/dev/null 2>&1; then
    echo "Error: timeout command not found" >&2
    exit 1
fi

if [ ! -x "$ABC_BIN" ]; then
    echo "Error: ABC binary not found or not executable: $ABC_BIN" >&2
    exit 1
fi

shopt -s nullglob
files=("$PROJECT_ROOT"/benchmarks/aig-and-output/*.aig)
shopt -u nullglob

if [ ${#files[@]} -eq 0 ]; then
    echo "Error: no AIG files found in $PROJECT_ROOT/benchmarks/aig-and-output" >&2
    exit 1
fi

echo "filename,result,time_sec" > "$OUTPUT_CSV"
echo "Starting ABC benchmark (timeout=${TIMEOUT_SEC}s) on ${#files[@]} files..."

for f in "${files[@]}"; do
    base="$(basename "$f")"
    tmp_out="$(mktemp)"

    if timeout "$TIMEOUT_SEC" "$ABC_BIN" -c "&r $f; &cec -m; quit" >"$tmp_out" 2>&1; then
        status=0
    else
        status=$?
    fi

    if [ "$status" -eq 124 ]; then
        result="TIMEOUT"
        time_sec="$TIMEOUT_SEC"
    else
        if grep -q "Networks are equivalent" "$tmp_out"; then
            result="UNSAT"
        elif grep -q "Networks are NOT EQUIVALENT" "$tmp_out"; then
            result="SAT"
        else
            result="UNKNOWN"
        fi

        time_sec="$(sed -nE 's/.*Time = *([0-9]+(\.[0-9]+)?).*/\1/p' "$tmp_out" | tail -n1)"
        if [ -z "$time_sec" ]; then
            time_sec="$result"
        fi
    fi

    echo "$base,$result,$time_sec" >> "$OUTPUT_CSV"
    rm -f "$tmp_out"
done

echo "Done. Results saved to $OUTPUT_CSV"
