#!/bin/bash
# @file finder.sh
# @author krish shah
# @brief Implements a shell script to search all the files in the given 
# directory (filesdir) for the (searchstr). It then prints out the number 
# of files in the directory and all subdirectories and the number of lines
# which contain the (searchstr). This script will return 1 with error if 
# 2 arguments are not specified, or if filesdir is not a valid directory
# Usage: 
# ./finder.sh [filesdir] [searchstr]

# check if 2 positional arguments are provided to the
# script, if not exit with return value 1
if [ $# -ne 2 ]; 
then
    echo 'ERROR: invalid number of arguments'
    echo 'EXPECTED USAGE: ./finder.sh [filesdir] [searchstr]'
    exit 1
fi

filesdir=$1
searchstr=$2

# if filesdir is not a valid directory, exit with return 
# value 1
if [ ! -d $filesdir ];
then 
    echo "$filesdir is not a valid directory!"
    exit 1
fi

# list all files in the filesdir using the find command 
# and pipe the output to wc to count the number of lines
# (each file will occupy one line)
X=$(find "$filesdir" -type f  | wc -l)

# grep filesdir for searchstr, and suppress error messages
# pipe the output to wc to count the number of lines
# (grep will output each line where searchstr is found)
Y=$(grep -r -s $searchstr $filesdir | wc -l)

echo "The number of files are ${X} and the number of matching lines are ${Y}"