#!/bin/bash

GREEN='\033[0;32m'
RED='\033[0;31m'
NONE='\033[0m'

WZ=./wzip
PZ=./pzip

if [ ! -x ./wzip ]; then
    echo "$WZ does not exit or is not executable"
    exit 1
fi

if [ ! -x ./pzip ]; then
    echo "$PZ does not exit or is not executable"
    exit 1
fi

allsuccess=0

echo "Test case 1: One file"

{ ./wzip test.txt; } > wzip.z
{ ./pzip test.txt; } > pzip.z

diff wzip.z pzip.z

if [[ $? == 0 ]]; then
    echo -e "No diff...\t\t\t${GREEN}ok${NONE}"
else 
    echo -e "Diff...\t\t\t\t${RED}failed${NONE}"
    exit 1
fi

echo "Test case 2: Multiple files"
{ ./wzip test.txt test.txt test.txt test.txt; } > wzip.z
{ ./pzip test.txt test.txt test.txt test.txt; } > pzip.z

diff wzip.z pzip.z

if [[ $? == 0 ]]; then
    echo -e "No diff...\t\t\t${GREEN}ok${NONE}"
else 
    echo -e "Diff...\t\t\t\t${RED}failed${NONE}"
    exit 1
fi

echo "Test case 3: Invalid file"

msg="open error: No such file or directory"

{ ./pzip invalid.txt; } &> pzip.z
status=$?
rval=$(cat pzip.z)

if [[ $status == 1 && $rval == $msg ]]; then
    echo -e "Invalid file...\t\t\t${GREEN}ok${NONE}"
else 
    echo -e "Invalid file...\t\t\t${RED}failed${NONE}"
    exit 1
fi


if [[ $allsuccess == 0 ]]; then
    echo "All tests passed!"
    exit 0
else 
    echo "Tests failed"
    exit 1
fi
