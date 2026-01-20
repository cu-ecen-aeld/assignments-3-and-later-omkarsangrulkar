#!/bin/sh

# Check if both arguments are provided
if [ $# -ne 2 ]; then
    echo "Error: Missing arguments."
    echo "Usage: $0 <filesdir> <searchstr>"
    exit 1
fi

filesdir="$1"
searchstr="$2"

# Check if filesdir is a valid directory
if [ ! -d "$filesdir" ]; then
    echo "Error: $filesdir is not a directory."
    exit 1
fi

# Count the number of files
X=$(find "$filesdir" -type f | wc -l)

# Count the number of matching lines containing searchstr
Y=$(grep -R "$searchstr" "$filesdir" 2>/dev/null | wc -l)

# Print the result
echo "The number of files are $X and the number of matching lines are $Y"

exit 0

