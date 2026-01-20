#!/bin/sh

# Check if both arguments are provided
if [ $# -ne 2 ]; then
    echo "Error: Missing arguments."
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

writefile="$1"
writestr="$2"

# Create the directory path if it does not exist
writedir=$(dirname "$writefile")

if ! mkdir -p "$writedir"; then
    echo "Error: Could not create directory path $writedir"
    exit 1
fi

# Write the string to the file
if ! echo "$writestr" > "$writefile"; then
    echo "Error: Could not create or write to file $writefile"
    exit 1
fi

exit 0

