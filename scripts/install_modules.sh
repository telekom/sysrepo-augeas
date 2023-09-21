#!/usr/bin/env bash

# YANG_DIR - directory with all the YANG modules to be installed with all the imports
# BINARY_DIR - directory with ay_startup binary and srds_augeas.so plugin
if [ -z "$YANG_DIR" -o -z "$BINARY_DIR" ]; then
    echo "Required environment variables not defined!"
    exit 1
fi

# optional env variable override
if [ -n "$SYSREPOCTL_EXECUTABLE" ]; then
    SYSREPOCTL="$SYSREPOCTL_EXECUTABLE"
# avoid problems with sudo PATH
elif [ `id -u` -eq 0 ]; then
    SYSREPOCTL=`su -c 'command -v sysrepoctl' -l $USER`
else
    SYSREPOCTL=`command -v sysrepoctl`
fi

if [ -n "$SYSREPO_PLUGIND_EXECUTABLE" ]; then
    SYSREPO_PLUGIND="$SYSREPO_PLUGIND_EXECUTABLE"
elif [ `id -u` -eq 0 ]; then
    SYSREPO_PLUGIND=`su -c 'command -v sysrepo-plugind' -l $USER`
else
    SYSREPO_PLUGIND=`command -v sysrepoplugind`
fi

# install augeas DS plugin
echo "-- Installing Augeas SR startup datastore plugin..."
$SYSREPOCTL -P "$BINARY_DIR/srds_augeas.so"

# install augeas sysrepo-plugind plugin
echo "-- Installing Augeas sysrepo-plugind plugin..."
$SYSREPO_PLUGIND -P "$BINARY_DIR/srplgd_augeas.so"

# get current modules
SCTL_MODULES=`$SYSREPOCTL -l`

for LENS in "$@"; do
    # check that it is not installed already
    SCTL_MODULE=`echo "$SCTL_MODULES" | grep "^$LENS \+|[^|]*| I"`
    if [ -n "$SCTL_MODULE" ]; then
        continue
    fi

    echo "-- Installing Augeas YANG module $LENS..."

    # store startup data of the module in a file
    TMPFILE=`mktemp -u`.xml
    "$BINARY_DIR/ay_startup" $LENS > "$TMPFILE"

    # install the YANG module
    $SYSREPOCTL -s "$YANG_DIR" -i "$YANG_DIR/$LENS.yang" -m "startup:augeas DS" -I "$TMPFILE"

    # remove the tmp file
    rm "$TMPFILE"
done
