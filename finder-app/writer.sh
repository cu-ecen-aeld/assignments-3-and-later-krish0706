#!/bin/bash


if [ $# -ne 2 ]; 
then
    echo 'ERROR: invalid number of arguements'
    echo 'EXPECTED USAGE: ./writer.sh [writefile] [writestr]'
    exit 1
fi

writefile=$1
writestr=$2

writefilepath=$(dirname $writefile)

if [ ! -d $writefilepath ]; 
then 
    mkdir -p $writefilepath
fi

echo "${writestr}" > $writefile

if [ $? -ne 0 ]; then
    echo "ERROR: file could not be created!"
    exit 1
fi