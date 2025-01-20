#!/bin/bash
# @file writer.sh
# @author krish shah
# @brief Implements a shell script to write the (writestr) to the 
# (writefile), the writefile is the full path of the file on the 
# filesystem. This script creates a new file and path if it does 
# not exist or overwrites the existing file. This script will 
# return 1 with error if 2 arguments are not specified, or if
# it is unable to create the file
# Usage:
# ./write.sh [writefile] [writestr]

# check if 2 positional arguments are provided to the
# script, if not exit with return value 1
if [ $# -ne 2 ]; 
then
    echo 'ERROR: invalid number of arguements'
    echo 'EXPECTED USAGE: ./writer.sh [writefile] [writestr]'
    exit 1
fi

writefile=$1
writestr=$2

# use dirname to strip the filename from the file path
writefilepath=$(dirname $writefile)

# if writefilepath is not a valid directory, create it
if [ ! -d $writefilepath ]; 
then 
    mkdir -p $writefilepath
fi

echo "${writestr}" > $writefile

# if writefile was not created, exit with return value 1
if [ $? -ne 0 ]; then
    echo "ERROR: file could not be created!"
    exit 1
fi