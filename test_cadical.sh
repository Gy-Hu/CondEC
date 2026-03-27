#!/bin/bash

# ==============================
# Configuration — update these paths based on your environment
# ==============================
SOLVER="/data2/wenbin/cadical/build/cadical"
CNF_DIR="/benchmarks/cnf"
OUTPUT_CSV="results_cadical.csv"
TIMEOUT_SEC=3600


if ! command -v /usr/bin/time &> /dev/null; then
    echo "❌ Error: GNU time not found. Install with: sudo apt install time"
    exit 1
fi

if ! command -v timeout &> /dev/null; then
    echo "❌ Error: 'timeout' command not found (part of coreutils)"
    exit 1
fi

if [ ! -x "$SOLVER" ]; then
    echo "❌ Error: Solver not found or not executable: $SOLVER"
    exit 1
fi

if [ ! -d "$CNF_DIR" ]; then
    echo "❌ Error: CNF directory not found: $CNF_DIR"
    exit 1
fi

echo "filename,status,real_time_sec,exit_code" > "$OUTPUT_CSV"
echo "✅ Starting benchmark (timeout=${TIMEOUT_SEC}s) on $(ls "$CNF_DIR"/*.cnf 2>/dev/null | wc -l) files..."

for cnf in "$CNF_DIR"/*.cnf; do
    [ -f "$cnf" ] || continue

    filename=$(basename "$cnf")
    echo "⏳ Running $filename (timeout ${TIMEOUT_SEC}s)..."

    tmp_out=$(mktemp)
    tmp_time=$(mktemp)

    timeout "$TIMEOUT_SEC" /usr/bin/time -f "%e" -o "$tmp_time" -- "$SOLVER" "$cnf" -q > "$tmp_out" 2>&1
    timeout_exit=$?

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

        raw_time=$(tail -n1 "$tmp_time" | tr -d '[:space:]')
        if [[ "$raw_time" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
            real_time=$(printf "%.3f" "$raw_time")
        else
            real_time="-1.000"
        fi
    fi

    echo "\"$filename\",\"$status\",$real_time,$exit_code" >> "$OUTPUT_CSV"

    rm -f "$tmp_out" "$tmp_time"
done

echo "✅ Done! Results saved to $OUTPUT_CSV"