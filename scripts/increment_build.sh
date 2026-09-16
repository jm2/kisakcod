#!/bin/bash

# Check if path is provided
if [[ -z "$1" ]]; then
    echo "Usage: $0 <path-to-src-folder>"
    exit 1
fi

BUILD_DIR="$1"
BUILD_FILE="$BUILD_DIR/buildnumber.txt"
HEADER_FILE="$BUILD_DIR/buildnumber.h"

# Optional third argument: the immutable source commit resolved by
# scripts/extern/resolve_source_identity.cmake. Empty when the caller cannot
# establish one; the generated header records the empty string rather than
# inventing a revision.
SOURCE_COMMIT="${3:-}"

# Check if a build number was not provided
if [[ -z "$2" ]]; then
    # Read existing build number or start at 0
    if [[ -f "$BUILD_FILE" ]]; then
        BUILD_NUMBER=$(<"$BUILD_FILE")
    else
        BUILD_NUMBER=0
    fi

    # Increment
    BUILD_NUMBER=$((BUILD_NUMBER + 1))
else
    BUILD_NUMBER="$2"
fi


# Write new number back
echo "$BUILD_NUMBER" > "$BUILD_FILE"

# Generate header
cat <<EOF > "$HEADER_FILE"
#pragma once
#define BUILD_NUMBER $BUILD_NUMBER
#define KISAK_SOURCE_COMMIT "$SOURCE_COMMIT"

char* getBuildNumber();
int getBuildNumberAsInt();
EOF

echo "Updated build number to $BUILD_NUMBER"
