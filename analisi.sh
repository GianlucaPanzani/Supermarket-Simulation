#!/bin/bash

if ! [ $# = 1 ]; then
    echo "Usage: try with logfile name after $0"
fi

cat $1 | tr / "\n"
rm logfile; touch logfile
