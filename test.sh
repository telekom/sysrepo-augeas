#!/usr/bin/env bash

# @file test.sh
# @author Adam Piecek <piecek@cesnet.cz>
# @brief Basic test script.
#
# Copyright (c) 2021 CESNET, z.s.p.o.
#
# This source code is licensed under BSD 3-Clause License (the "License").
# You may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://opensource.org/licenses/BSD-3-Clause

for entry in "checkpoint"/*
do
    # get filename without .yang suffix
    filename=$(basename $entry .yang)
    # run augyang
    augyang=$(build/augyang -s $filename)
    # get content of checkpoint
    checkpoint=$(cat $entry)
    # call diff ( <() is a "process substitution" )
    diff <(echo "$checkpoint") <(echo "$augyang")
    # check return code
    if [ $? -ne 0 ]; then
        echo "Error for file $entry"
    fi
    # check by yanglint
    yanglint augeas-extension.yang $entry
    if [ $? -ne 0 ]; then
        echo "yanglint failed for file $entry"
    fi
done
