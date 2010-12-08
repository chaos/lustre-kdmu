#!/bin/bash
# vim:expandtab:shiftwidth=4:softtabstop=4:tabstop=4:
#
# Run select tests by setting ONLY, or as arguments to the script.
# Skip specific tests by setting EXCEPT.
#
# e.g. ONLY="22 23" or ONLY="`seq 32 39`" or EXCEPT="31"
set -e

ONLY=${ONLY:-"$*"}
# bug number for skipped test:
ALWAYS_EXCEPT=${ALWAYS_EXCEPT:-"$SANITY_GSS_EXCEPT"}
# UPDATE THE COMMENT ABOVE WITH BUG NUMBERS WHEN CHANGING ALWAYS_EXCEPT!

# Tests that fail on uml
CPU=`awk '/model/ {print $4}' /proc/cpuinfo`
[ "$CPU" = "UML" ] && EXCEPT="$EXCEPT"

case `uname -r` in
2.6*) FSTYPE=${FSTYPE:-ldiskfs}; ALWAYS_EXCEPT="$ALWAYS_EXCEPT " ;;
*) error "unsupported kernel (gss only works with 2.6.x)" ;;
esac

SRCDIR=`dirname $0`
export PATH=$PWD/$SRCDIR:$SRCDIR:$SRCDIR/../utils:$SRCDIR/../utils/gss:$PATH:/sbin
export NAME=${NAME:-local}
SAVE_PWD=$PWD

CLEANUP=${CLEANUP:-""}
SETUP=${SETUP:-""}

LUSTRE=${LUSTRE:-`dirname $0`/..}
. $LUSTRE/tests/test-framework.sh
init_test_env $@
. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}
init_logging

require_dsh_mds || exit 0

[ "$SLOW" = "no" ] && EXCEPT_SLOW="100 101"

# $RUNAS_ID may get set incorrectly somewhere else
[ $UID -eq 0 -a $RUNAS_ID -eq 0 ] && error "\$RUNAS_ID set to 0, but \$UID is also 0!"

# remove $SEC, we'd like to control everything by ourselves
unset SEC

#
# global variables of this sanity
#
KRB5_CCACHE_DIR=/tmp
KRB5_CRED=$KRB5_CCACHE_DIR/krb5cc_$RUNAS_ID
KRB5_CRED_SAVE=$KRB5_CCACHE_DIR/krb5cc.sanity.save
DBENCH_PID=0

# set manually
GSS=true
GSS_KRB5=true

prepare_krb5_creds() {
    echo prepare krb5 cred
    rm -f $KRB5_CRED_SAVE
    echo RUNAS=$RUNAS
    $RUNAS krb5_login.sh || exit 1
    [ -f $KRB5_CRED ] || exit 2
    echo CRED=$KRB5_CRED
    cp $KRB5_CRED $KRB5_CRED_SAVE
}

prepare_krb5_creds

# we want double mount
MOUNT_2=${MOUNT_2:-"yes"}
check_and_setup_lustre

rm -rf $DIR/[df][0-9]*

check_runas_id $RUNAS_ID $RUNAS_ID $RUNAS

build_test_filter

start_dbench()
{
    NPROC=`cat /proc/cpuinfo 2>/dev/null | grep ^processor | wc -l`
    [ $NPROC -gt 2 ] && NPROC=2
    sh rundbench $NPROC 1>/dev/null &
    DBENCH_PID=$!
    sleep 2

    num=`ps --no-headers -p $DBENCH_PID 2>/dev/null | wc -l`
    if [ $num -ne 1 ]; then
        error "failed to start dbench $NPROC"
    else
        echo "started dbench with $NPROC processes at background"
    fi

    return 0
}

check_dbench()
{
    num=`ps --no-headers -p $DBENCH_PID 2>/dev/null | wc -l`
    if [ $num -eq 0 ]; then
        echo "dbench $DBENCH_PID already finished"
        wait $DBENCH_PID || error "dbench $PID exit with error"
        start_dbench
    elif [ $num -ne 1 ]; then
        killall -9 dbench
        error "found $num instance of pid $DBENCH_PID ???"
    fi

    return 0
}

stop_dbench()
{
    for ((;;)); do
        killall dbench 2>/dev/null
        num=`ps --no-headers -p $DBENCH_PID | wc -l`
        if [ $num -eq 0 ]; then
            echo "dbench finished"
            break
        fi
        echo "dbench $DBENCH_PID is still running, waiting 2s..."
        sleep 2
    done

    wait $DBENCH_PID || true
    sync || true
}

restore_krb5_cred() {
    cp $KRB5_CRED_SAVE $KRB5_CRED
    chown $RUNAS_ID:$RUNAS_ID $KRB5_CRED
    chmod 0600 $KRB5_CRED
}

check_multiple_gss_daemons() {
    local facet=$1
    local gssd=$2
    local gssd_name=`basename $gssd`

    for ((i=0;i<10;i++)); do
        do_facet $facet "$gssd -v &"
    done

    # wait daemons entering "stable" status
    sleep 5

    num=`do_facet $facet ps -o cmd -C $gssd_name | grep $gssd_name | wc -l`
    echo "$num instance(s) of $gssd_name are running"

    if [ $num -ne 1 ]; then
        error "$gssd_name not unique"
    fi
}

calc_connection_cnt
umask 077

test_0() {
    local my_facet=mds

    echo "bring up gss daemons..."
    start_gss_daemons

    echo "check with someone already running..."
    check_multiple_gss_daemons $my_facet $LSVCGSSD
    if $GSS_PIPEFS; then
        check_multiple_gss_daemons $my_facet $LGSSD
    fi

    echo "check with someone run & finished..."
    do_facet $my_facet killall -q -2 lgssd lsvcgssd || true
    sleep 5 # wait fully exit
    check_multiple_gss_daemons $my_facet $LSVCGSSD
    if $GSS_PIPEFS; then
        check_multiple_gss_daemons $my_facet $LGSSD
    fi

    echo "check refresh..."
    do_facet $my_facet killall -q -2 lgssd lsvcgssd || true
    sleep 5 # wait fully exit
    do_facet $my_facet ipcrm -S 0x3b92d473
    check_multiple_gss_daemons $my_facet $LSVCGSSD
    if $GSS_PIPEFS; then
        do_facet $my_facet ipcrm -S 0x3a92d473
        check_multiple_gss_daemons $my_facet $LGSSD
    fi
}
run_test 0 "start multiple gss daemons"

set_flavor_all krb5p

test_1() {
    local file=$DIR/$tfile

    chmod 0777 $DIR || error "chmod $DIR failed"
    $RUNAS touch $DIR
    # access w/o cred
    $RUNAS kdestroy
    $RUNAS $LFS flushctx $MOUNT || error "can't flush context on $MOUNT"
    $RUNAS touch $file && error "unexpected success"

    # access w/ cred
    restore_krb5_cred
    $RUNAS touch $file || error "should not fail"
    [ -f $file ] || error "$file not found"
}
run_test 1 "access with or without krb5 credential"

test_2() {
    local file1=$DIR/$tfile-1
    local file2=$DIR/$tfile-2

    chmod 0777 $DIR || error "chmod $DIR failed"
    # current access should be ok
    $RUNAS touch $file1 || error "can't touch $file1"
    [ -f $file1 ] || error "$file1 not found"

    # cleanup all cred/ctx and touch
    $RUNAS kdestroy
    $RUNAS $LFS flushctx $MOUNT || error "can't flush context on $MOUNT"
    $RUNAS touch $file2 && error "unexpected success"

    # restore and touch
    restore_krb5_cred
    $RUNAS touch $file2 || error "should not fail"
    [ -f $file2 ] || error "$file2 not found"
}
run_test 2 "lfs flushctx"

test_3() {
    local file=$DIR/$tfile

    # create file
    echo "aaaaaaaaaaaaaaaaa" > $file
    chmod 0666 $file
    $CHECKSTAT -p 0666 $file || error "$UID checkstat error"
    $RUNAS $CHECKSTAT -p 0666 $file || error "$RUNAS_ID checkstat error"
    $RUNAS cat $file > /dev/null || error "$RUNAS_ID cat error"

    # start multiop
    $RUNAS multiop $file o_r &
    OPPID=$!
    # wait multiop finish its open()
    sleep 1

    # cleanup all cred/ctx and check
    # metadata check should fail, but file data check should success
    # because we always use root credential to OSTs
    $RUNAS kdestroy
    $RUNAS $LFS flushctx $MOUNT || error "can't flush context on $MOUNT"
    echo "destroied credentials/contexs for $RUNAS_ID"
    $RUNAS $CHECKSTAT -p 0666 $file && error "checkstat succeed"
    kill -s 10 $OPPID
    wait $OPPID || error "read file data failed"
    echo "read file data OK"

    # restore and check again
    restore_krb5_cred
    echo "restored credentials for $RUNAS_ID"
    $RUNAS $CHECKSTAT -p 0666 $file || error "$RUNAS_ID checkstat (2) error"
    echo "$RUNAS_ID checkstat OK"
    $CHECKSTAT -p 0666 $file || error "$UID checkstat (2) error"
    echo "$UID checkstat OK"
    $RUNAS cat $file > /dev/null || error "$RUNAS_ID cat (2) error"
    echo "$RUNAS_ID read file data OK"
}
run_test 3 "local cache under DLM lock"

test_4() {
    local file1=$DIR/$tfile-1
    local file2=$DIR/$tfile-2

    ! $GSS_PIPEFS && skip "pipefs not used" && return

    chmod 0777 $DIR || error "chmod $DIR failed"
    # current access should be ok
    $RUNAS touch $file1 || error "can't touch $file1"
    [ -f $file1 ] || error "$file1 not found"

    # stop lgssd
    send_sigint client lgssd
    sleep 5
    check_gss_daemon_facet client lgssd && error "lgssd still running"

    # flush context, and touch
    $RUNAS $LFS flushctx $MOUNT || error "can't flush context on $MOUNT"
    $RUNAS touch $file2 &
    TOUCHPID=$!
    echo "waiting touch pid $TOUCHPID"
    wait $TOUCHPID && error "touch should fail"

    # restart lgssd
    do_facet client "$LGSSD -v"
    sleep 5
    check_gss_daemon_facet client lgssd

    # touch new should succeed
    $RUNAS touch $file2 || error "can't touch $file2"
    [ -f $file2 ] || error "$file2 not found"
}
run_test 4 "lgssd dead, operations should wait timeout and fail"

test_5() {
    local file1=$DIR/$tfile-1
    local file2=$DIR/$tfile-2
    local wait_time=$((TIMEOUT + TIMEOUT / 2))

    chmod 0777 $DIR || error "chmod $DIR failed"
    # current access should be ok
    $RUNAS touch $file1 || error "can't touch $file1"
    [ -f $file1 ] || error "$file1 not found"

    # stop lsvcgssd
    send_sigint $(comma_list $(mdts_nodes)) lsvcgssd
    sleep 5
    check_gss_daemon_nodes $(comma_list $(mdts_nodes)) lsvcgssd && error "lsvcgssd still running"

    # flush context, and touch
    $RUNAS $LFS flushctx $MOUNT || error "can't flush context on $MOUNT"
    $RUNAS touch $file2 &
    TOUCHPID=$!

    # wait certain time
    echo "waiting $wait_time seconds for touch pid $TOUCHPID"
    sleep $wait_time
    num=`ps --no-headers -p $TOUCHPID | wc -l`
    [ $num -eq 1 ] || error "touch already ended ($num)"
    echo "process $TOUCHPID still hanging there... OK"

    # restart lsvcgssd, expect touch suceed
    echo "restart lsvcgssd and recovering"
    start_gss_daemons $(comma_list $(mdts_nodes)) "$LSVCGSSD -v"
    sleep 5
    check_gss_daemon_nodes $(comma_list $(mdts_nodes)) lsvcgssd
    wait $TOUCHPID || error "touch fail"
    [ -f $file2 ] || error "$file2 not found"
}
run_test 5 "lsvcgssd dead, operations lead to recovery"

test_6() {
    local nfile=10

    mkdir $DIR/d6 || error "mkdir $DIR/d6 failed"
    for ((i=0; i<$nfile; i++)); do
        dd if=/dev/zero of=$DIR/d6/file$i bs=8k count=1 || error "dd file$i failed"
    done
    ls -l $DIR/d6/* > /dev/null || error "ls failed"
    rm -rf $DIR2/d6/* || error "rm failed"
    rmdir $DIR2/d6/ || error "rmdir failed"
}
run_test 6 "test basic DLM callback works"

test_7() {
    local tdir=$DIR/d7
    local num_osts

    #
    # for open(), client only reserve space for default stripe count lovea,
    # and server may return larger lovea in reply (because of larger stripe
    # count), client need call enlarge_reqbuf() and save the replied lovea
    # in request for future possible replay.
    #
    # Note: current script does NOT guarantee enlarge_reqbuf() will be in
    # the path, however it does work in local test which has 2 OSTs and
    # default stripe count is 1.
    #
    num_osts=`$LFS getstripe $MOUNT | egrep "^[0-9]*:.*ACTIVE" | wc -l`
    echo "found $num_osts active OSTs"
    [ $num_osts -lt 2 ] && echo "skipping $TESTNAME (must have >= 2 OSTs)" && return

    mkdir $tdir || error
    $LFS setstripe -c $num_osts $tdir || error

    echo "creating..."
    for ((i=0;i<20;i++)); do
        dd if=/dev/zero of=$tdir/f$i bs=4k count=16 2>/dev/null
    done
    echo "reading..."
    for ((i=0;i<20;i++)); do
        dd if=$tdir/f$i of=/dev/null bs=4k count=16 2>/dev/null
    done
    rm -rf $tdir
}
run_test 7 "exercise enlarge_reqbuf()"

test_8()
{
    local ATHISTORY=$(do_facet $SINGLEMDS "find /sys/ -name at_history")
    local ATOLDBASE=$(do_facet $SINGLEMDS "cat $ATHISTORY")
    local REQ_DELAY
    do_facet $SINGLEMDS "echo 8 >> $ATHISTORY"

    mkdir -p $DIR/d8
    chmod a+w $DIR/d8

    $LCTL dk > /dev/null
    debugsave
    sysctl -w lnet.debug="+other"

    # wait for the at estimation come down, this is faster
    while [ true ]; do
        REQ_DELAY=`lctl get_param -n mdc.${FSNAME}-MDT0000-mdc-*.timeouts |
                   awk '/portal 12/ {print $5}' | tail -1`
        [ $REQ_DELAY -le 5 ] && break
        echo "current AT estimation is $REQ_DELAY, wait a little bit"
        sleep 8
    done
    REQ_DELAY=$((${REQ_DELAY} + ${REQ_DELAY} / 4 + 5))

    # sleep sometime in ctx handle
    do_facet $SINGLEMDS lctl set_param fail_val=$REQ_DELAY
#define OBD_FAIL_SEC_CTX_HDL_PAUSE       0x1204
    do_facet $SINGLEMDS lctl set_param fail_loc=0x1204

    $RUNAS $LFS flushctx $MOUNT || error "can't flush context on $MOUNT"

    $RUNAS touch $DIR/d8/f &
    TOUCHPID=$!
    echo "waiting for touch (pid $TOUCHPID) to finish..."
    sleep 2 # give it a chance to really trigger context init rpc
    do_facet $SINGLEMDS sysctl -w lustre.fail_loc=0
    wait $TOUCHPID || error "touch should have succeeded"

    $LCTL dk | grep "Early reply #" || error "No early reply"

    debugrestore
    do_facet $SINGLEMDS "echo $ATOLDBASE >> $ATHISTORY" || true
}
run_test 8 "Early reply sent for slow gss context negotiation"

#
# following tests will manipulate flavors and may end with any flavor set,
# so each test should not assume any start flavor.
#

test_90() {
    if [ "$SLOW" = "no" ]; then
        total=10
    else
        total=60
    fi

    restore_to_default_flavor
    set_rule $FSNAME any any krb5p
    wait_flavor all2all krb5p

    start_dbench

    for ((n=0;n<$total;n++)); do
        sleep 2
        check_dbench
        echo "flush ctx ($n/$total) ..."
        $LFS flushctx $MOUNT || error "can't flush context on $MOUNT"
    done
    check_dbench
    #sleep to let ctxs be re-established
    sleep 10
    stop_dbench
}
run_test 90 "recoverable from losing contexts under load"

test_99() {
    local nrule_old=0
    local nrule_new=0
    local max=64

    #
    # general rules
    #
    nrule_old=`do_facet mgs lctl get_param -n mgs.MGS.live.$FSNAME 2>/dev/null \
               | grep "$FSNAME.srpc.flavor." | wc -l`
    echo "original general rules: $nrule_old"

    for ((i = $nrule_old; i < $max; i++)); do
        set_rule $FSNAME elan$i any krb5n || error "set rule $i"
    done
    for ((i = $nrule_old; i < $max; i++)); do
        set_rule $FSNAME elan$i any || error "remove rule $i"
    done

    nrule_new=`do_facet mgs lctl get_param -n mgs.MGS.live.$FSNAME 2>/dev/null \
               | grep "$FSNAME.srpc.flavor." | wc -l`
    if [ $nrule_new != $nrule_old ]; then
        error "general rule: $nrule_new != $nrule_old"
    fi

    #
    # target-specific rules
    #
    nrule_old=`do_facet mgs lctl get_param -n mgs.MGS.live.$FSNAME 2>/dev/null \
               | grep "$FSNAME-MDT0000.srpc.flavor." | wc -l`
    echo "original target rules: $nrule_old"

    for ((i = $nrule_old; i < $max; i++)); do
        set_rule $FSNAME-MDT0000 elan$i any krb5i || error "set rule $i"
    done
    for ((i = $nrule_old; i < $max; i++)); do
        set_rule $FSNAME-MDT0000 elan$i any || error "remove rule $i"
    done

    nrule_new=`do_facet mgs lctl get_param -n mgs.MGS.live.$FSNAME 2>/dev/null \
               | grep "$FSNAME-MDT0000.srpc.flavor." | wc -l`
    if [ $nrule_new != $nrule_old ]; then
        error "general rule: $nrule_new != $nrule_old"
    fi
}
run_test 99 "set large number of sptlrpc rules"

error_dbench()
{
    local err_str=$1

    killall -9 dbench
    sleep 1

    error $err_str
}

test_100() {
    # started from default flavors
    restore_to_default_flavor

    # running dbench background
    start_dbench

    #
    # all: null -> krb5n -> krb5a -> krb5i -> krb5p -> plain
    #
    set_rule $FSNAME any any krb5n
    wait_flavor all2all krb5n || error_dbench "1"
    check_dbench

    set_rule $FSNAME any any krb5a
    wait_flavor all2all krb5a || error_dbench "2"
    check_dbench

    set_rule $FSNAME any any krb5i
    wait_flavor all2all krb5i || error_dbench "3"
    check_dbench

    set_rule $FSNAME any any krb5p
    wait_flavor all2all krb5p || error_dbench "4"
    check_dbench

    set_rule $FSNAME any any plain
    wait_flavor all2all plain || error_dbench "5"
    check_dbench

    #
    # M - M: krb5a
    # C - M: krb5i
    # M - O: krb5p
    # C - O: krb5n
    #
    set_rule $FSNAME any mdt2mdt krb5a
    wait_flavor mdt2mdt krb5a || error_dbench "6"
    check_dbench

    set_rule $FSNAME any cli2mdt krb5i
    wait_flavor cli2mdt krb5i || error_dbench "7"
    check_dbench

    set_rule $FSNAME any mdt2ost krb5p
    wait_flavor mdt2ost krb5p || error_dbench "8"
    check_dbench

    set_rule $FSNAME any cli2ost krb5n
    wait_flavor cli2ost krb5n || error_dbench "9"
    check_dbench

    #
    # * - MDT0: krb5p
    # * - OST0: krb5i
    #
    # nothing should be changed because they are override by above dir rules
    #
    set_rule $FSNAME-MDT0000 any any krb5p
    set_rule $FSNAME-OST0000 any any krb5i
    wait_flavor mdt2mdt krb5a || error_dbench "10"
    wait_flavor cli2mdt krb5i || error_dbench "11"
    check_dbench
    wait_flavor mdt2ost krb5p || error_dbench "12"
    wait_flavor cli2ost krb5n || error_dbench "13"

    #
    # delete all dir-specific rules
    #
    set_rule $FSNAME any mdt2mdt
    set_rule $FSNAME any cli2mdt
    set_rule $FSNAME any mdt2ost
    set_rule $FSNAME any cli2ost
    wait_flavor mdt2mdt krb5p $((MDSCOUNT - 1)) || error_dbench "14"
    wait_flavor cli2mdt krb5p $(get_clients_mount_count) || error_dbench "15"
    check_dbench
    wait_flavor mdt2ost krb5i $MDSCOUNT || error_dbench "16"
    wait_flavor cli2ost krb5i $(get_clients_mount_count) || error_dbench "17"
    check_dbench

    #
    # remove:
    #  * - MDT0: krb5p
    #  * - OST0: krb5i
    #
    set_rule $FSNAME-MDT0000 any any
    set_rule $FSNAME-OST0000 any any || error_dbench "18"
    wait_flavor all2all plain || error_dbench "19"
    check_dbench

    stop_dbench
}
run_test 100 "change security flavor on the fly under load"

switch_sec_test()
{
    local flavor0=$1
    local flavor1=$2
    local filename=$DIR/$tfile
    local multiop_pid
    local num

    #
    # after set to flavor0, start multop which use flavor0 rpc, and let
    # server drop the reply; then switch to flavor1, the resend should be
    # completed using flavor1. To exercise the code of switching ctx/sec
    # for a resend request.
    #
    log ">>>>>>>>>>>>>>> Testing $flavor0 -> $flavor1 <<<<<<<<<<<<<<<<<<<"

    set_rule $FSNAME any cli2mdt $flavor0
    wait_flavor cli2mdt $flavor0
    rm -f $filename || error "remove old $filename failed"

#MDS_REINT = 36
#define OBD_FAIL_PTLRPC_DROP_REQ_OPC     0x513
    do_facet $SINGLEMDS lctl set_param fail_val=36
    do_facet $SINGLEMDS lctl set_param fail_loc=0x513
    log "starting multiop"
    multiop $filename m &
    multiop_pid=$!
    echo "multiop pid=$multiop_pid"
    sleep 1

    set_rule $FSNAME any cli2mdt $flavor1
    wait_flavor cli2mdt $flavor1

    num=`ps --no-headers -p $multiop_pid 2>/dev/null | wc -l`
    [ $num -eq 1 ] || error "multiop($multiop_pid) already ended ($num)"
    echo "process $multiop_pid is still hanging there... OK"

    do_facet $SINGLEMDS lctl set_param fail_loc=0
    log "waiting for multiop ($multiop_pid) to finish"
    wait $multiop_pid || error "multiop returned error"
}

test_101()
{
    # started from default flavors
    restore_to_default_flavor

    switch_sec_test null  plain
    switch_sec_test plain krb5n
    switch_sec_test krb5n krb5a
    switch_sec_test krb5a krb5i
    switch_sec_test krb5i krb5p
    switch_sec_test krb5p null
    switch_sec_test null  krb5p
    switch_sec_test krb5p krb5i
    switch_sec_test krb5i plain
    switch_sec_test plain krb5p
}
run_test 101 "switch ctx/sec for resending request"

error_102()
{
    local err_str=$1

    killall -9 dbench
    sleep 1

    error $err_str
}

test_102() {
    # started from default flavors
    restore_to_default_flavor

    # run dbench background
    start_dbench

    echo "Testing null->krb5n->krb5a->krb5i->krb5p->plain->null"
    set_rule $FSNAME any any krb5n
    set_rule $FSNAME any any krb5a
    set_rule $FSNAME any any krb5i
    set_rule $FSNAME any any krb5p
    set_rule $FSNAME any any plain
    set_rule $FSNAME any any null

    check_dbench
    wait_flavor all2all null || error_dbench "1"
    check_dbench

    echo "waiting for 15s and check again"
    sleep 15
    check_dbench

    echo "Testing null->krb5i->null->krb5i->null..."
    for ((i=0; i<10; i++)); do
        set_rule $FSNAME any any krb5i
        set_rule $FSNAME any any null
    done
    set_rule $FSNAME any any krb5i

    check_dbench
    wait_flavor all2all krb5i || error_dbench "2"
    check_dbench

    echo "waiting for 15s and check again"
    sleep 15
    check_dbench

    stop_dbench
}
run_test 102 "survive from insanely fast flavor switch"

test_150() {
    local save_opts
    local count
    local clients=$CLIENTS

    [ -z $clients ] && clients=$HOSTNAME

    # started from default flavors
    restore_to_default_flavor

    # at this time no rules has been set on mgs; mgc use null
    # flavor connect to mgs.
    count=`flvr_cnt_mgc2mgs null`
    [ $count -eq 1 ] || error "$count mgc connection use null flavor"

    zconf_umount_clients $clients $MOUNT || return 1

    # mount client with conflict flavor - should fail
    save_opts=$MOUNTOPT
    MOUNTOPT="$MOUNTOPT,mgssec=krb5p"
    zconf_mount_clients $clients $MOUNT && \
        error "mount with conflict flavor should have failed"
    MOUNTOPT=$save_opts

    # mount client with same flavor - should succeed
    save_opts=$MOUNTOPT
    MOUNTOPT="$MOUNTOPT,mgssec=null"
    zconf_mount_clients $clients $MOUNT || \
        error "mount with same flavor should have succeeded"
    MOUNTOPT=$save_opts
    zconf_umount_clients $clients $MOUNT || return 2

    # mount client with default flavor - should succeed
    zconf_mount_clients $clients $MOUNT || \
        error "mount with default flavor should have succeeded"
}
run_test 150 "secure mgs connection: client flavor setting"

test_151() {
    local save_opts

    # set mgs only accept krb5p
    set_rule _mgs any any krb5p

    # umount everything, modules still loaded
    stopall

    # mount mgs with default flavor, in current framework it means mgs+mdt1.
    # the connection of mgc of mdt1 to mgs is expected fail.
    DEVNAME=$(mdsdevname 1)
    start mds1 $DEVNAME $MDS_MOUNT_OPTS && error "mount with default flavor should have failed"

    # mount with unauthorized flavor should fail
    save_opts=$MDS_MOUNT_OPTS
    MDS_MOUNT_OPTS="$MDS_MOUNT_OPTS,mgssec=null"
    start mds1 $DEVNAME $MDS_MOUNT_OPTS && error "mount with unauthorized flavor should have failed"
    MDS_MOUNT_OPTS=$save_opts

    # mount with designated flavor should succeed
    save_opts=$MDS_MOUNT_OPTS
    MDS_MOUNT_OPTS="$MDS_MOUNT_OPTS,mgssec=krb5p"
    start mds1 $DEVNAME $MDS_MOUNT_OPTS || error "mount with designated flavor should have succeeded"
    MDS_MOUNT_OPTS=$save_opts

    stop mds1 -f
}
run_test 151 "secure mgs connection: server flavor control"

equals_msg `basename $0`: test complete, cleaning up
check_and_cleanup_lustre
[ -f "$TESTSUITELOG" ] && cat $TESTSUITELOG && grep -q FAIL $TESTSUITELOG && exit 1 || true
