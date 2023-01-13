#!/usr/bin/env bash

set -e

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <lens-patch-dir> <augeas-lens-dir>"
    exit 1
fi

LENS_PATCH_DIR=$1
AUGEAS_LENS_DIR=$2

# download master lenses
wget -O augeas.tar.gz https://github.com/hercules-team/augeas/archive/master.tar.gz 2> /dev/null
tar -xvf augeas.tar.gz augeas-master/lenses 1> /dev/null

# apply patches
for LENS_P in `find $LENS_PATCH_DIR -name '*.patch'`; do
    LENS=`echo $LENS_P | grep -o '[^/]*$' | grep -o '^[^.]*\\.[^.]*'`
    patch augeas-master/lenses/$LENS $LENS_P
done

# install master patched lenses
cp augeas-master/lenses/*.aug $AUGEAS_LENS_DIR

# copy new lenses
cp $LENS_PATCH_DIR/*.aug $AUGEAS_LENS_DIR
