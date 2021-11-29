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

if [ -d "./yang_expected" ]; then
    # script runs where it is
    checkdir="./yang_expected"
    augyangPath="../build"
    extensionPath="../modules/"
elif [ -d "../../tests/yang_expected" ]; then
    # script run in build/tests
    checkdir="../../tests/yang_expected"
    augyangPath="../"
    extensionPath="../../modules/"
else
    echo $(pwd)
    echo "Error: yang_expected directory not found"
    exit 1
fi

retcode=0

for entry in $checkdir/*
do
    # get filename without .yang suffix
    filename=$(basename $entry .yang)
    # run augyang
    augyang=$($augyangPath/augyang -s $filename)
    # get content of yang_expected
    yang_expected=$(cat $entry)
    # call diff ( <() is a "process substitution" )
    diff <(echo "$yang_expected") <(echo "$augyang")
    # check return code
    if [ $? -ne 0 ]; then
        echo "Error for file $entry"
        retcode=1
    fi
    # check by yanglint
    yanglint $extensionPath/augeas-extension.yang $entry
    if [ $? -ne 0 ]; then
        echo "yanglint failed for file $entry"
        retcode=1
    fi
done

exit $retcode
