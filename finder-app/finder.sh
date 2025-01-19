#!/bin/bash

if [ $# -ne 2 ]; 
then
    echo 'ERROR: invalid number of arguements'
    echo 'EXPECTED USAGE: ./finder.sh [filesdir] [searchstr]'
    exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d $filesdir ];
then 
    echo "$filesdir is not a valid directory!"
    exit 1
fi

X=$(find "$filesdir" -type f  | wc -l)
Y=$(grep -r -s $searchstr $filesdir | wc -l)

echo "The number of files are ${X} and the number of matching lines are ${Y}"