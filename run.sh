#!/bin/bash

# Flag to indicate whether to skip hour.md
SKIP_hour=false
OUTPUT_FILE="simulator_output.log"

# Parse command-line arguments
while getopts "h" opt; do
    case $opt in
        h)
            SKIP_hour=true
            ;;
        *)
            echo "Usage: $0 [-h]"
            echo "  -h   Skip processing the file named hour.md"
            exit 1
            ;;
    esac
done

# Clear the output file if it exists
> "$OUTPUT_FILE"

# Run the make commands
make scheduler
if [ $? -ne 0 ]; then
    echo "Error: 'make scheduler' failed." | tee -a "$OUTPUT_FILE"
    exit 1
fi

make simulator
if [ $? -ne 0 ]; then
    echo "Error: 'make simulator' failed." | tee -a "$OUTPUT_FILE"
    exit 1
fi

# Loop through all .md files in the Test Cases directory
for file in $(find "Test_Cases" -type f -name "*.md"); do
    # Skip hour.md if -h flag is provided
    if $SKIP_hour && [[ "$(basename "$file")" == "hour.md" ]]; then
        echo "Skipping hour.md as per -h flag" | tee -a "$OUTPUT_FILE"
        continue
    fi

    echo "Processing $file..." | tee -a "$OUTPUT_FILE"
    echo "------------------------" | tee -a "$OUTPUT_FILE"
    ./simulator "$file" >> "$OUTPUT_FILE" 2>&1
    if [ $? -ne 0 ]; then
        echo "Error: Failed to process $file with ./simulator. See log for details." | tee -a "$OUTPUT_FILE"
    fi
    echo "------------------------" | tee -a "$OUTPUT_FILE"
done

echo "Processing complete. Output written to $OUTPUT_FILE."
