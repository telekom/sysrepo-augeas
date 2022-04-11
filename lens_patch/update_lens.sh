#!/usr/bin/env bash

set -e

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <lens-patch-dir> <augeas-lens-dir>"
    exit 1
fi

LENS_PATCH_DIR=$1
AUGEAS_LENS_DIR=$2

wget -O augeas.tar.gz https://github.com/hercules-team/augeas/archive/master.tar.gz 2> /dev/null
tar -xvf augeas.tar.gz augeas-master/lenses 1> /dev/null
for LENS_P in `ls $LENS_PATCH_DIR/*.patch`; do
    LENS=`echo $LENS_P | grep -o '[^/]*$' | grep -o '^[^.]*\\.[^.]*'`
    patch augeas-master/lenses/$LENS $LENS_P
done
cp augeas-master/lenses/*.aug $AUGEAS_LENS_DIR
