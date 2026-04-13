#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
SOLVER="${KISSAT_BIN:-$PROJECT_ROOT/tools/kissat/build/kissat}"
CNF_DIR="$PROJECT_ROOT/benchmarks/cnf"
OUTPUT_CSV="$PROJECT_ROOT/results_kissat.csv"
TIMEOUT_SEC="${TIMEOUT_SEC:-3600}"

if ! command -v /usr/bin/time >/dev/null 2>&1; then
    echo "Error: GNU time not found at /usr/bin/time" >&2
    exit 1
fi

if ! command -v timeout >/dev/null 2>&1; then
    echo "Error: timeout command not found" >&2
    exit 1
fi

if [ ! -x "$SOLVER" ]; then
    echo "Error: solver not found or not executable: $SOLVER" >&2
    exit 1
fi

shopt -s nullglob
files=("$CNF_DIR"/*.cnf)
shopt -u nullglob

if [ ${#files[@]} -eq 0 ]; then
    echo "Error: no CNF files found in $CNF_DIR" >&2
    exit 1
fi

echo "filename,status,real_time_sec,exit_code" > "$OUTPUT_CSV"
echo "Starting Kissat benchmark (timeout=${TIMEOUT_SEC}s) on ${#files[@]} files..."

for cnf in "${files[@]}"; do
    filename="$(basename "$cnf")"
    tmp_out="$(mktemp)"
    tmp_time="$(mktemp)"

    if timeout "$TIMEOUT_SEC" /usr/bin/time -f "%e" -o "$tmp_time" -- "$SOLVER" "$cnf" -q > "$tmp_out" 2>&1; then
        timeout_exit=0
    else
        timeout_exit=$?
    fi

    if [ "$timeout_exit" -eq 124 ]; then
        status="TIMEOUT"
        real_time="${TIMEOUT_SEC}.000"
        exit_code=124
    else
        exit_code=$timeout_exit
        status="UNKNOWN"

        if grep -q "^s[[:space:]]\+UNSATISFIABLE" "$tmp_out"; then
            status="UNSATISFIABLE"
        elif grep -q "^s[[:space:]]\+SATISFIABLE" "$tmp_out"; then
            status="SATISFIABLE"
        fi

        raw_time="$(tr -d '[:space:]' < "$tmp_time")"
        if [[ "$raw_time" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
            real_time="$(printf "%.3f" "$raw_time")"
        else
            real_time="-1.000"
        fi
    fi

    echo "\"$filename\",\"$status\",$real_time,$exit_code" >> "$OUTPUT_CSV"
    rm -f "$tmp_out" "$tmp_time"
done

echo "Done. Results saved to $OUTPUT_CSV"
