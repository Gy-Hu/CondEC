#!/bin/bash

OUTPUT="results.csv"
TIMEOUT_SEC=3600

echo "filename,result,time_seconds" > "$OUTPUT"

for file in benchmarks/aig/*.aig; do
    [ -f "$file" ] || continue

    basename=$(basename "$file")

    output=$(timeout "$TIMEOUT_SEC" ./condec "$file" -q 2>&1)
    exit_code=$?

    if [ $exit_code -eq 124 ]; then
        result="TIMEOUT"
        time_sec="$TIMEOUT_SEC"
        echo "⏰ Timeout: $basename"
    elif [ $exit_code -eq 0 ]; then
        if [[ $output =~ ,\ ([A-Z]+),\ time:\ ([0-9]+\.[0-9]+)\ s ]]; then
            result="${BASH_REMATCH[1]}"
            time_sec="${BASH_REMATCH[2]}"
        else
            result="PARSE_ERROR"
            time_sec="0"
            echo "⚠️ Parse failed for $basename: '$output'" >&2
        fi
    else
        result="ERROR_EXIT_$exit_code"
        time_sec="0"
        echo "❌ Error (exit $exit_code) for $basename" >&2
    fi

    echo "$basename,$result,$time_sec" >> "$OUTPUT"
done

echo "✅ Batch completed! Results saved in $OUTPUT"