#!/bin/bash
#
# This test was used in a set of CMD3 tests (cmd3-7 test).

# File attribute retrieval rate for small file creation
# 3200 ops/sec for single node 29,000 ops/sec aggregate

# In a dir containing 10 million non-striped files, the mdsrate Test Program
# will perform directory ordered stat's (readdir) for 10 minutes. This test
# will be run from a single node for #1 and from all nodes for #2
# aggregate test to measure stat performance.

LUSTRE=${LUSTRE:-`dirname $0`/..}
. $LUSTRE/tests/test-framework.sh
init_test_env $@
. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}

assert_env CLIENTS MDSRATE SINGLECLIENT MPIRUN

MACHINEFILE=${MACHINEFILE:-$TMP/$(basename $0 .sh).machines}
# Do not use name [df][0-9]* to avoid cleanup by rm, bug 18045
BASEDIR=$MOUNT/mdsrate
TESTDIR=$BASEDIR/stat

# Requirements
NUM_FILES=${NUM_FILES:-1000000}
TIME_PERIOD=${TIME_PERIOD:-600}                        # seconds

# --random_order (default) -OR- --readdir_order
DIR_ORDER=${DIR_ORDER:-"--readdir_order"}

LOG=${TESTSUITELOG:-$TMP/$(basename $0 .sh).log}
CLIENT=$SINGLECLIENT
NODES_TO_USE=${NODES_TO_USE:-$CLIENTS}
NUM_CLIENTS=$(get_node_count ${NODES_TO_USE//,/ })

rm -f $LOG

[ ! -x ${MDSRATE} ] && error "${MDSRATE} not built."

log "===== $0 ====== " 

check_and_setup_lustre

mkdir -p $BASEDIR
chmod 0777 $BASEDIR
$LFS setstripe $BASEDIR -i 0 -c 1
get_stripe $BASEDIR

IFree=$(mdsrate_inodes_available)
if [ $IFree -lt $NUM_FILES ]; then
    NUM_FILES=$IFree
fi

generate_machine_file $NODES_TO_USE $MACHINEFILE || error "can not generate machinefile"

if [ -n "$NOCREATE" ]; then
    echo "NOCREATE=$NOCREATE  => no file creation."
else
    mdsrate_cleanup $NUM_CLIENTS $MACHINEFILE $NUM_FILES $TESTDIR 'f%%d' --ignore

    log "===== $0 Test preparation: creating ${NUM_FILES} files."

    COMMAND="${MDSRATE} ${MDSRATE_DEBUG} --mknod --dir ${TESTDIR}
                        --nfiles ${NUM_FILES} --filefmt 'f%%d'"
    echo "+" ${COMMAND}

    NUM_CLIENTS=$(get_node_count ${NODES_TO_USE//,/ })
    NUM_THREADS=$((NUM_CLIENTS * MDSCOUNT))
    if [ $NUM_CLIENTS -gt 50 ]; then
        NUM_THREADS=$NUM_CLIENTS
    fi

    mpi_run -np ${NUM_THREADS} -machinefile ${MACHINEFILE} ${COMMAND} 2>&1
    [ ${PIPESTATUS[0]} != 0 ] && error "mdsrate file creation failed, aborting"

fi

COMMAND="${MDSRATE} ${MDSRATE_DEBUG} --stat --time ${TIME_PERIOD}
        --dir ${TESTDIR} --nfiles ${NUM_FILES} --filefmt 'f%%d'
        ${DIR_ORDER} ${SEED_OPTION}"

# 1
if [ -n "$NOSINGLE" ]; then
    echo "NO Test for stats on a single client."
else
    log "===== $0 ### 1 NODE STAT ###"
    echo "+" ${COMMAND}

    mpi_run -np 1 -machinefile ${MACHINEFILE} ${COMMAND} | tee ${LOG}
    
    if [ ${PIPESTATUS[0]} != 0 ]; then
        [ -f $LOG ] && sed -e "s/^/log: /" $LOG
        error "mdsrate on a single client failed, aborting"
    fi
fi

# 2
[ $NUM_CLIENTS -eq 1 ] && NOMULTI=yes
if [ -n "$NOMULTI" ]; then
    echo "NO test for stats on multiple nodes."
else
    log "===== $0 ### ${NUM_CLIENTS} NODES STAT ###"
    echo "+" ${COMMAND}

    mpi_run -np ${NUM_CLIENTS} -machinefile ${MACHINEFILE} ${COMMAND} | tee ${LOG}

    if [ ${PIPESTATUS[0]} != 0 ]; then
        [ -f $LOG ] && sed -e "s/^/log: /" $LOG
        error "mdsrate stats on multiple nodes failed, aborting"
    fi
fi

equals_msg `basename $0`: test complete, cleaning up
mdsrate_cleanup $NUM_CLIENTS $MACHINEFILE $NUM_FILES $TESTDIR 'f%%d'
rmdir $BASEDIR || true
rm -f $MACHINEFILE
check_and_cleanup_lustre
#rm -f $LOG

exit 0
