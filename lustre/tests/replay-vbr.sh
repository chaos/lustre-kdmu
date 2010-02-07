#!/bin/bash

set -e

# bug number:  16356
ALWAYS_EXCEPT="2     $REPLAY_VBR_EXCEPT"

SAVE_PWD=$PWD
PTLDEBUG=${PTLDEBUG:--1}
LUSTRE=${LUSTRE:-`dirname $0`/..}
SETUP=${SETUP:-""}
CLEANUP=${CLEANUP:-""}
. $LUSTRE/tests/test-framework.sh

init_test_env $@

. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}

[ "$SLOW" = "no" ] && EXCEPT_SLOW=""

[ -n "$CLIENTS" ] || { skip_env "Need two or more clients" && exit 0; }
[ $CLIENTCOUNT -ge 2 ] || \
    { skip_env "Need two or more clients, have $CLIENTCOUNT" && exit 0; }

remote_mds_nodsh && skip "remote MDS with nodsh" && exit 0
[ ! "$NAME" = "ncli" ] && ALWAYS_EXCEPT="$ALWAYS_EXCEPT"
[ "$NAME" = "ncli" ] && MOUNT_2=""
MOUNT_2=""
build_test_filter

check_and_setup_lustre
rm -rf $DIR/[df][0-9]*

[ "$DAEMONFILE" ] && $LCTL debug_daemon start $DAEMONFILE $DAEMONSIZE

rmultiop_start() {
    local client=$1
    local file=$2
    local cmds=$3

    # We need to run do_node in bg, because pdsh does not exit
    # if child process of run script exists.
    # I.e. pdsh does not exit when runmultiop_bg_pause exited,
    # because of multiop_bg_pause -> $MULTIOP_PROG &
    # By the same reason we need sleep a bit after do_nodes starts
    # to let runmultiop_bg_pause start muliop and
    # update /tmp/multiop_bg.pid ;
    # The rm /tmp/multiop_bg.pid guarantees here that
    # we have the updated by runmultiop_bg_pause
    # /tmp/multiop_bg.pid file

    local pid_file=$TMP/multiop_bg.pid.$$
    do_node $client "rm -f $pid_file && MULTIOP_PID_FILE=$pid_file LUSTRE= runmultiop_bg_pause $file $cmds" &
    local pid=$!
    sleep 3
    local multiop_pid
    multiop_pid=$(do_node $client cat $pid_file)
    [ -n "$multiop_pid" ] || error "$client : Can not get multiop_pid from $pid_file "
    eval export $(client_var_name $client)_multiop_pid=$multiop_pid
    eval export $(client_var_name $client)_do_node_pid=$pid
    local var=$(client_var_name $client)_multiop_pid
    echo client $client multiop_bg started multiop_pid=${!var}
    return $?
}

rmultiop_stop() {
    local client=$1
    local multiop_pid=$(client_var_name $client)_multiop_pid
    local do_node_pid=$(client_var_name $client)_do_node_pid

    echo "Stopping multiop_pid=${!multiop_pid} (kill ${!multiop_pid} on $client)"
    do_node $client kill -USR1 ${!multiop_pid}

    wait ${!do_node_pid}
}

get_version() {
    local var=${SINGLEMDS}_svc
    local client=$1
    local file=$2
    local fid

    fid=$(do_node $client $LFS path2fid $file)
    do_facet $SINGLEMDS $LCTL --device ${!var} getobjversion $fid
}

test_0a() {
    local file=$DIR/$tfile
    local pre
    local post

    do_node $CLIENT1 mcreate $file
    pre=$(get_version $CLIENT1 $file)
    do_node $CLIENT1 openfile -f O_RDWR $file
    post=$(get_version $CLIENT1 $file)
    if (($pre != $post)); then
        error "version changed unexpectedly: pre $pre, post $post"
    fi
}
run_test 0a "open and close do not change versions"

test_0b() {
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 mkdir -p -m 755 $DIR/$tdir

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 777 $DIR/$tdir
    do_node $CLIENT1 openfile -f O_RDWR:O_CREAT $DIR/$tdir/$tfile
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if ! do_node $CLIENT1 $CHECKSTAT -a $DIR/$tdir/$tfile; then
        error "open succeeded unexpectedly"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0b "open (O_CREAT) checks version of parent"

test_0c() {
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 mkdir -p -m 755 $DIR/$tdir
    do_node $CLIENT1 openfile -f O_RDWR:O_CREAT -m 0644 $DIR/$tdir/$tfile

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 777 $DIR/$tdir
    do_node $CLIENT2 chmod 666 $DIR/$tdir/$tfile
    rmultiop_start $CLIENT1 $DIR/$tdir/$tfile o_c
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS
    client_up $CLIENT1 || error "$CLIENT1 evicted"

    rmultiop_stop $CLIENT1 || error "close failed"
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0c "open (non O_CREAT) does not checks versions"

test_0d() {
    local pre
    local post

    pre=$(get_version $CLIENT1 $DIR)
    do_node $CLIENT1 mkfifo $DIR/$tfile
    post=$(get_version $CLIENT1 $DIR)
    if (($pre == $post)); then
        error "version not changed: pre $pre, post $post"
    fi
}
run_test 0d "create changes version of parent"

test_0e() {
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 mkdir -p -m 755 $DIR/$tdir

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 777 $DIR/$tdir
    do_node $CLIENT1 mkfifo $DIR/$tdir/$tfile
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if ! do_node $CLIENT1 $CHECKSTAT -a $DIR/$tdir/$tfile; then
        error "create succeeded unexpectedly"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0e "create checks version of parent"

test_0f() {
    local pre
    local post

    do_node $CLIENT1 mcreate $DIR/$tfile
    pre=$(get_version $CLIENT1 $DIR)
    do_node $CLIENT1 rm $DIR/$tfile
    post=$(get_version $CLIENT1 $DIR)
    if (($pre == $post)); then
        error "version not changed: pre $pre, post $post"
    fi
}
run_test 0f "unlink changes version of parent"

test_0g() {
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 mkdir -p -m 755 $DIR/$tdir
    do_node $CLIENT1 mcreate $DIR/$tdir/$tfile

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 777 $DIR/$tdir
    do_node $CLIENT1 rm $DIR/$tdir/$tfile
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if do_node $CLIENT1 $CHECKSTAT -a $DIR/$tdir/$tfile; then
        error "unlink succeeded unexpectedly"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0g "unlink checks version of parent"

test_0h() {
    local file=$DIR/$tfile
    local pre
    local post

    do_node $CLIENT1 mcreate $file
    pre=$(get_version $CLIENT1 $file)
    do_node $CLIENT1 chown $RUNAS_ID $file
    post=$(get_version $CLIENT1 $file)
    if (($pre == $post)); then
        error "version not changed: pre $pre, post $post"
    fi
}
run_test 0h "setattr of UID changes versions"

test_0i() {
    local file=$DIR/$tfile
    local pre
    local post

    do_node $CLIENT1 mcreate $file
    pre=$(get_version $CLIENT1 $file)
    do_node $CLIENT1 chown :$RUNAS_ID $file
    post=$(get_version $CLIENT1 $file)
    if (($pre == $post)); then
        error "version not changed: pre $pre, post $post"
    fi
}
run_test 0i "setattr of GID changes versions"

test_0j() {
    local file=$DIR/$tfile
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 mcreate $file

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chown :$RUNAS_ID $file
    do_node $CLIENT1 chown $RUNAS_ID $file
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if ! do_node $CLIENT1 $CHECKSTAT -u \\\#$UID $file; then
        error "setattr of UID succeeded unexpectedly"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0j "setattr of UID checks versions"

test_0k() {
    local file=$DIR/$tfile
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 mcreate $file

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chown $RUNAS_ID $file
    do_node $CLIENT1 chown :$RUNAS_ID $file
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if ! do_node $CLIENT1 $CHECKSTAT -g \\\#$UID $file; then
        error "setattr of GID succeeded unexpectedly"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0k "setattr of GID checks versions"

test_0l() {
    local file=$DIR/$tfile
    local pre
    local post

    do_node $CLIENT1 openfile -f O_RDWR:O_CREAT -m 0644 $file
    pre=$(get_version $CLIENT1 $file)
    do_node $CLIENT1 chmod 666 $file
    post=$(get_version $CLIENT1 $file)
    if (($pre == $post)); then
        error "version not changed: pre $pre, post $post"
    fi
}
run_test 0l "setattr of permission changes versions"

test_0m() {
    local file=$DIR/$tfile
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 openfile -f O_RDWR:O_CREAT -m 0644 $file

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chown :$RUNAS_ID $file
    do_node $CLIENT1 chmod 666 $file
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if ! do_node $CLIENT1 $CHECKSTAT -p 0644 $file; then
        error "setattr of permission succeeded unexpectedly"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0m "setattr of permission checks versions"

test_0n() {
    local file=$DIR/$tfile
    local pre
    local post

    do_node $CLIENT1 mcreate $file
    pre=$(get_version $CLIENT1 $file)
    do_node $CLIENT1 chattr +i $file
    post=$(get_version $CLIENT1 $file)
    do_node $CLIENT1 chattr -i $file
    if (($pre == $post)); then
        error "version not changed: pre $pre, post $post"
    fi
}
run_test 0n "setattr of flags changes versions"

checkattr() {
    local client=$1
    local attr=$2
    local file=$3
    local rc

    if ((${#attr} != 1)); then
        error "checking multiple attributes not implemented yet"
    fi
    do_node $client lsattr $file | cut -d ' ' -f 1 | grep -q $attr
}

test_0o() {
    local file=$DIR/$tfile
    local rc
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 openfile -f O_RDWR:O_CREAT -m 0644 $file

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 666 $file
    do_node $CLIENT1 chattr +i $file
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    checkattr $CLIENT1 i $file
    rc=$?
    do_node $CLIENT1 chattr -i $file
    if [ $rc -eq 0 ]; then
        error "setattr of flags succeeded unexpectedly"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0o "setattr of flags checks versions"

test_0p() {
    local file=$DIR/$tfile
    local pre
    local post
    local ad_orig
    local var=${SINGLEMDS}_svc

    ad_orig=$(do_facet $SINGLEMDS "$LCTL get_param mdd.${!var}.atime_diff")
    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.atime_diff=0"
    do_node $CLIENT1 mcreate $file
    pre=$(get_version $CLIENT1 $file)
    do_node $CLIENT1 touch $file
    post=$(get_version $CLIENT1 $file)
    #
    # We don't fail MDS in this test.  atime_diff shall be
    # restored to its original value.
    #
    do_facet $SINGLEMDS "$LCTL set_param $ad_orig"
    if (($pre != $post)); then
        error "version changed unexpectedly: pre $pre, post $post"
    fi
}
run_test 0p "setattr of times does not change versions"

test_0q() {
    local file=$DIR/$tfile
    local pre
    local post

    do_node $CLIENT1 mcreate $file
    pre=$(get_version $CLIENT1 $file)
    do_node $CLIENT1 truncate $file 1
    post=$(get_version $CLIENT1 $file)
    if (($pre != $post)); then
        error "version changed unexpectedly: pre $pre, post $post"
    fi
}
run_test 0q "setattr of size does not change versions"

test_0r() {
    local file=$DIR/$tfile
    local mtime_pre
    local mtime_post
    local mtime
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.atime_diff=0"
    do_node $CLIENT1 openfile -f O_RDWR:O_CREAT -m 0644 $file

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 666 $file
    do_node $CLIENT1 truncate $file 1
    sleep 1
    mtime_pre=$(do_node $CLIENT1 stat --format=%Y $file)
    do_node $CLIENT1 touch $file
    mtime_post=$(do_node $CLIENT1 stat --format=%Y $file)
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_up $CLIENT1 || error "$CLIENT1 evicted"
    if (($mtime_pre >= $mtime_post)); then
        error "time not changed: pre $mtime_pre, post $mtime_post"
    fi
    if ! do_node $CLIENT1 $CHECKSTAT -s 1 $file; then
        error "setattr of size failed"
    fi
    mtime=$(do_node $CLIENT1 stat --format=%Y $file)
    if (($mtime != $mtime_post)); then
        error "setattr of times failed: expected $mtime_post, got $mtime"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0r "setattr of times and size does not check versions"

test_0s() {
    local pre
    local post
    local tp_pre
    local tp_post

    do_node $CLIENT1 mcreate $DIR/$tfile
    do_node $CLIENT1 mkdir -p $DIR/$tdir
    pre=$(get_version $CLIENT1 $DIR/$tfile)
    tp_pre=$(get_version $CLIENT1 $DIR/$tdir)
    do_node $CLIENT1 link $DIR/$tfile $DIR/$tdir/$tfile
    post=$(get_version $CLIENT1 $DIR/$tfile)
    tp_post=$(get_version $CLIENT1 $DIR/$tdir)
    if (($pre == $post)); then
        error "version of source not changed: pre $pre, post $post"
    fi
    if (($tp_pre == $tp_post)); then
        error "version of target parent not changed: pre $tp_pre, post $tp_post"
    fi
}
run_test 0s "link changes versions of source and target parent"

test_0t() {
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 mcreate $DIR/$tfile
    do_node $CLIENT1 mkdir -p -m 755 $DIR/$tdir

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 777 $DIR/$tdir
    do_node $CLIENT1 link $DIR/$tfile $DIR/$tdir/$tfile
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if ! do_node $CLIENT1 $CHECKSTAT -a $DIR/$tdir/$tfile; then
        error "link should fail"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0t "link checks version of target parent"

test_0u() {
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 openfile -f O_RDWR:O_CREAT -m 0644 $DIR/$tfile
    do_node $CLIENT1 mkdir -p $DIR/$tdir

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 666 $DIR/$tfile
    do_node $CLIENT1 link $DIR/$tfile $DIR/$tdir/$tfile
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if ! do_node $CLIENT1 $CHECKSTAT -a $DIR/$tdir/$tfile; then
        error "link should fail"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0u "link checks version of source"

test_0v() {
    local sp_pre
    local tp_pre
    local sp_post
    local tp_post

    do_node $CLIENT1 mcreate $DIR/$tfile
    do_node $CLIENT1 mkdir -p $DIR/$tdir
    sp_pre=$(get_version $CLIENT1 $DIR)
    tp_pre=$(get_version $CLIENT1 $DIR/$tdir)
    do_node $CLIENT1 mv $DIR/$tfile $DIR/$tdir/$tfile
    sp_post=$(get_version $CLIENT1 $DIR)
    tp_post=$(get_version $CLIENT1 $DIR/$tdir)
    if (($sp_pre == $sp_post)); then
        error "version of source parent not changed: pre $sp_pre, post $sp_post"
    fi
    if (($tp_pre == $tp_post)); then
        error "version of target parent not changed: pre $tp_pre, post $tp_post"
    fi
}
run_test 0v "rename changes versions of source parent and target parent"

test_0w() {
    local pre
    local post

    do_node $CLIENT1 mcreate $DIR/$tfile
    pre=$(get_version $CLIENT1 $DIR)
    do_node $CLIENT1 mv $DIR/$tfile $DIR/$tfile-new
    post=$(get_version $CLIENT1 $DIR)
    if (($pre == $post)); then
        error "version of parent not changed: pre $pre, post $post"
    fi
}
run_test 0w "rename within same dir changes version of parent"

test_0x() {
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 mcreate $DIR/$tfile
    do_node $CLIENT1 mkdir -p -m 755 $DIR/$tdir

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 777 $DIR
    do_node $CLIENT1 mv $DIR/$tfile $DIR/$tdir/$tfile
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if do_node $CLIENT1 $CHECKSTAT -a $DIR/$tfile; then
        error "rename should fail"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0x "rename checks version of source parent"

test_0y() {
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    do_node $CLIENT1 mcreate $DIR/$tfile
    do_node $CLIENT1 mkdir -p -m 755 $DIR/$tdir

    replay_barrier $SINGLEMDS
    do_node $CLIENT2 chmod 777 $DIR/$tdir
    do_node $CLIENT1 mv $DIR/$tfile $DIR/$tdir/$tfile
    zconf_umount $CLIENT2 $MOUNT
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1 not evicted"
    if do_node $CLIENT1 $CHECKSTAT -a $DIR/$tfile; then
        error "rename should fail"
    fi
    zconf_mount $CLIENT2 $MOUNT
}
run_test 0y "rename checks version of target parent"

[ "$CLIENTS" ] && zconf_umount_clients $CLIENTS $DIR

test_1a() {
    echo "mount client $CLIENT1,$CLIENT2..."
    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    do_node $CLIENT2 mkdir -p $DIR/$tdir
    replay_barrier $SINGLEMDS
    do_node $CLIENT1 createmany -o $DIR/$tfile- 25
    do_node $CLIENT2 createmany -o $DIR/$tdir/$tfile-2- 1
    do_node $CLIENT1 createmany -o $DIR/$tfile-3- 25
    zconf_umount $CLIENT2 $DIR

    facet_failover $SINGLEMDS
    # recovery shouldn't fail due to missing client 2
    client_up $CLIENT1 || return 1

    # All 50 files should have been replayed
    do_node $CLIENT1 unlinkmany $DIR/$tfile- 25 || return 2
    do_node $CLIENT1 unlinkmany $DIR/$tfile-3- 25 || return 3

    zconf_mount $CLIENT2 $DIR || error "mount $CLIENT2 $DIR fail"
    [ -e $DIR/$tdir/$tfile-2-0 ] && error "$tfile-2-0 exists"

    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 1a "client during replay doesn't affect another one"

test_2a() {
    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    do_node $CLIENT2 mkdir -p $DIR/$tdir
    replay_barrier $SINGLEMDS
    do_node $CLIENT2 mcreate $DIR/$tdir/$tfile
    do_node $CLIENT1 createmany -o $DIR/$tfile- 25
    #client1 read data from client2 which will be lost
    do_node $CLIENT1 $CHECKSTAT $DIR/$tdir/$tfile
    do_node $CLIENT1 createmany -o $DIR/$tfile-3- 25
    zconf_umount $CLIENT2 $DIR

    facet_failover $SINGLEMDS
    # recovery shouldn't fail due to missing client 2
    client_up $CLIENT1 || return 1

    # All 50 files should have been replayed
    do_node $CLIENT1 unlinkmany $DIR/$tfile- 25 || return 2
    do_node $CLIENT1 unlinkmany $DIR/$tfile-3- 25 || return 3
    do_node $CLIENT1 $CHECKSTAT $DIR/$tdir/$tfile && return 4

    zconf_mount $CLIENT2 $DIR || error "mount $CLIENT2 $DIR fail"

    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 2a "lost data due to missed REMOTE client during replay"

#
# This test uses three Lustre clients on two hosts.
#
#   Lustre Client 1:    $CLIENT1:$MOUNT     ($DIR)
#   Lustre Client 2:    $CLIENT2:$MOUNT2    ($DIR2)
#   Lustre Client 3:    $CLIENT2:$MOUNT1    ($DIR1)
#
test_2b() {
    local pre
    local post
    local var=${SINGLEMDS}_svc

    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.sync_permission=0"
    zconf_mount $CLIENT1 $MOUNT
    zconf_mount $CLIENT2 $MOUNT2
    zconf_mount $CLIENT2 $MOUNT1
    do_node $CLIENT1 openfile -f O_RDWR:O_CREAT -m 0644 $DIR/$tfile-a
    do_node $CLIENT1 openfile -f O_RDWR:O_CREAT -m 0644 $DIR/$tfile-b

    #
    # Save an MDT transaction number before recovery.
    #
    pre=$(get_version $CLIENT1 $DIR/$tfile-a)

    #
    # Comments on the replay sequence state the expected result
    # of each request.
    #
    #   "R"     Replayed.
    #   "U"     Unable to replay.
    #   "J"     Rejected.
    #
    replay_barrier $SINGLEMDS
    do_node $CLIENT1 chmod 666 $DIR/$tfile-a            # R
    do_node $CLIENT2 chmod 666 $DIR1/$tfile-b           # R
    do_node $CLIENT2 chown :$RUNAS_ID $DIR2/$tfile-a    # U
    do_node $CLIENT1 chown $RUNAS_ID $DIR/$tfile-a      # J
    do_node $CLIENT2 truncate $DIR2/$tfile-b 1          # U
    do_node $CLIENT2 chown :$RUNAS_ID $DIR1/$tfile-b    # R
    do_node $CLIENT1 chown $RUNAS_ID $DIR/$tfile-b      # R
    zconf_umount $CLIENT2 $MOUNT2
    facet_failover $SINGLEMDS

    client_evicted $CLIENT1 || error "$CLIENT1:$MOUNT not evicted"
    client_up $CLIENT2 || error "$CLIENT2:$MOUNT1 evicted"

    #
    # Check the MDT epoch.  $post must be the first transaction
    # number assigned after recovery.
    #
    do_node $CLIENT2 touch $DIR1/$tfile
    post=$(get_version $CLIENT2 $DIR1/$tfile)
    if (($(($pre >> 32)) == $((post >> 32)))); then
        error "epoch not changed: pre $pre, post $post"
    fi
    if (($(($post & 0x00000000ffffffff)) != 1)); then
        error "transno should restart from one: got $post"
    fi

    do_node $CLIENT2 stat $DIR1/$tfile-a
    do_node $CLIENT2 stat $DIR1/$tfile-b

    do_node $CLIENT2 $CHECKSTAT -p 0666 -u \\\#$UID -g \\\#$UID \
            $DIR1/$tfile-a || error "$DIR/$tfile-a: unexpected state"
    do_node $CLIENT2 $CHECKSTAT -p 0666 -u \\\#$RUNAS_ID -g \\\#$RUNAS_ID \
            $DIR1/$tfile-b || error "$DIR/$tfile-b: unexpected state"

    zconf_umount $CLIENT2 $MOUNT1
    zconf_umount $CLIENT1 $MOUNT
}
run_test 2b "3 clients: some, none, and all reqs replayed"

test_3a() {
    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    #make sure the time will change
    local var=${SINGLEMDS}_svc
    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.atime_diff=0" || return
    do_node $CLIENT1 touch $DIR/$tfile
    do_node $CLIENT2 $CHECKSTAT $DIR/$tfile
    sleep 1
    replay_barrier $SINGLEMDS
    #change time
    do_node $CLIENT2 touch $DIR/$tfile
    do_node $CLIENT2 $CHECKSTAT $DIR/$tfile
    #another change
    do_node $CLIENT1 touch $DIR/$tfile
    #remove file
    do_node $CLIENT1 rm $DIR/$tfile
    zconf_umount $CLIENT2 $DIR

    facet_failover $SINGLEMDS
    # recovery shouldn't fail due to missing client 2
    client_up $CLIENT1 || return 1
    do_node $CLIENT1 $CHECKSTAT $DIR/$tfile && return 2

    zconf_mount $CLIENT2 $DIR || error "mount $CLIENT2 $DIR fail"

    zconf_umount_clients $CLIENTS $DIR

    return 0
}
run_test 3a "setattr of time/size doesn't change version"

test_3b() {
    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    #make sure the time will change
    local var=${SINGLEMDS}_svc
    do_facet $SINGLEMDS "$LCTL set_param mdd.${!var}.atime_diff=0" || return

    do_node $CLIENT1 touch $DIR/$tfile
    do_node $CLIENT2 $CHECKSTAT $DIR/$tfile
    sleep 1
    replay_barrier $SINGLEMDS
    #change mode
    do_node $CLIENT2 chmod +x $DIR/$tfile
    do_node $CLIENT2 $CHECKSTAT $DIR/$tfile
    #abother chmod
    do_node $CLIENT1 chmod -x $DIR/$tfile
    zconf_umount $CLIENT2 $DIR

    facet_failover $SINGLEMDS
    # recovery should fail due to missing client 2
    client_evicted $CLIENT1 || return 1

    do_node $CLIENT1 $CHECKSTAT -p 0755 $DIR/$tfile && return 2
    zconf_mount $CLIENT2 $DIR || error "mount $CLIENT2 $DIR fail"

    zconf_umount_clients $CLIENTS $DIR

    return 0
}
run_test 3b "setattr of permissions changes version"

vbr_deactivate_client() {
    local client=$1
    echo "Deactivating client $client";
    do_node $client "sysctl -w lustre.fail_loc=0x50d"
}

vbr_activate_client() {
    local client=$1
    echo "Activating client $client";
    do_node $client "sysctl -w lustre.fail_loc=0x0"
}

remote_server ()
{
    local client=$1
    [ -z "$(do_node $client lctl dl | grep mdt)" ] && \
    [ -z "$(do_node $client lctl dl | grep ost)" ]
}

test_4a() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    do_node $CLIENT2 mkdir -p $DIR/$tdir
    replay_barrier $SINGLEMDS
    do_node $CLIENT1 createmany -o $DIR/$tfile- 25
    do_node $CLIENT2 createmany -o $DIR/$tdir/$tfile-2- 25
    do_node $CLIENT1 createmany -o $DIR/$tfile-3- 25
    vbr_deactivate_client $CLIENT2

    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 1

    # All 50 files should have been replayed
    do_node $CLIENT1 unlinkmany $DIR/$tfile- 25 || return 2
    do_node $CLIENT1 unlinkmany $DIR/$tfile-3- 25 || return 3

    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 4
    # All 25 files from client2 should have been replayed
    do_node $CLIENT2 unlinkmany $DIR/$tdir/$tfile-2- 25 || return 5

    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 4a "fail MDS, delayed recovery"

test_4b(){
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    replay_barrier $SINGLEMDS
    do_node $CLIENT1 createmany -o $DIR/$tfile- 25
    do_node $CLIENT2 createmany -o $DIR/$tdir/$tfile-2- 25
    vbr_deactivate_client $CLIENT2

    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 1

    # create another set of files
    do_node $CLIENT1 createmany -o $DIR/$tfile-3- 25

    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 2

    # All files from should have been replayed
    do_node $CLIENT1 unlinkmany $DIR/$tfile- 25 || return 3
    do_node $CLIENT1 unlinkmany $DIR/$tfile-3- 25 || return 4
    do_node $CLIENT2 unlinkmany $DIR/$tdir/$tfile-2- 25 || return 5

    zconf_umount_clients $CLIENTS $DIR
}
run_test 4b "fail MDS, normal operation, delayed open recovery"

test_4c() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    replay_barrier $SINGLEMDS
    do_node $CLIENT1 createmany -m $DIR/$tfile- 25
    do_node $CLIENT2 createmany -m $DIR/$tdir/$tfile-2- 25
    vbr_deactivate_client $CLIENT2

    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 1

    # create another set of files
    do_node $CLIENT1 createmany -m $DIR/$tfile-3- 25

    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 2

    # All files from should have been replayed
    do_node $CLIENT1 unlinkmany $DIR/$tfile- 25 || return 3
    do_node $CLIENT1 unlinkmany $DIR/$tfile-3- 25 || return 4
    do_node $CLIENT2 unlinkmany $DIR/$tdir/$tfile-2- 25 || return 5

    zconf_umount_clients $CLIENTS $DIR
}
run_test 4c "fail MDS, normal operation, delayed recovery"

test_5a() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    replay_barrier $SINGLEMDS
    do_node $CLIENT1 createmany -o $DIR/$tfile- 25
    do_node $CLIENT2 createmany -o $DIR/$tfile-2- 1
    do_node $CLIENT1 createmany -o $DIR/$tfile-3- 1
    vbr_deactivate_client $CLIENT2

    facet_failover $SINGLEMDS
    client_evicted $CLIENT1 || return 1

    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 2

    # First 25 files should have been replayed
    do_node $CLIENT1 unlinkmany $DIR/$tfile- 25 || return 3
    # Third file is failed due to missed client2
    do_node $CLIENT1 $CHECKSTAT $DIR/$tfile-3-0 && error "$tfile-3-0 exists"
    # file from client2 should exists
    do_node $CLIENT2 unlinkmany $DIR/$tfile-2- 1 || return 4

    zconf_umount_clients $CLIENTS $DIR
}
run_test 5a "fail MDS, delayed recovery should fail"

test_5b() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    replay_barrier $SINGLEMDS
    do_node $CLIENT1 createmany -o $DIR/$tfile- 25
    do_node $CLIENT2 createmany -o $DIR/$tfile-2- 1
    vbr_deactivate_client $CLIENT2

    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 1
    do_node $CLIENT1 $CHECKSTAT $DIR/$tfile-2-0 && error "$tfile-2-0 exists"

    # create another set of files
    do_node $CLIENT1 createmany -o $DIR/$tfile-3- 25

    vbr_activate_client $CLIENT2
    client_evicted $CLIENT2 || return 4
    # file from client2 should fail
    do_node $CLIENT2 $CHECKSTAT $DIR/$tfile-2-0 && error "$tfile-2-0 exists"

    # All 50 files from client 1 should have been replayed
    do_node $CLIENT1 unlinkmany $DIR/$tfile- 25 || return 2
    do_node $CLIENT1 unlinkmany $DIR/$tfile-3- 25 || return 3

    zconf_umount_clients $CLIENTS $DIR
}
run_test 5b "fail MDS, normal operation, delayed recovery should fail"

test_6a() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    do_node $CLIENT2 mkdir -p $DIR/$tdir
    replay_barrier $SINGLEMDS
    do_node $CLIENT1 createmany -o $DIR/$tfile- 25
    do_node $CLIENT2 createmany -o $DIR/$tdir/$tfile-2- 25
    do_node $CLIENT1 createmany -o $DIR/$tfile-3- 25
    vbr_deactivate_client $CLIENT2

    facet_failover $SINGLEMDS
    # replay only 5 requests
    do_node $CLIENT2 "sysctl -w lustre.fail_val=5"
#define OBD_FAIL_PTLRPC_REPLAY        0x50e
    do_node $CLIENT2 "sysctl -w lustre.fail_loc=0x2000050e"
    client_up $CLIENT2
    # vbr_activate_client $CLIENT2
    # need way to know that client stops replays
    sleep 5

    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 1

    # All files should have been replayed
    do_node $CLIENT1 unlinkmany $DIR/$tfile- 25 || return 2
    do_node $CLIENT1 unlinkmany $DIR/$tfile-3- 25 || return 3
    do_node $CLIENT2 unlinkmany $DIR/$tdir/$tfile-2- 25 || return 5

    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 6a "fail MDS, delayed recovery, fail MDS"

test_7a() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    do_node $CLIENT2 mkdir -p $DIR/$tdir
    replay_barrier $SINGLEMDS
    do_node $CLIENT1 createmany -o $DIR/$tfile- 25
    do_node $CLIENT2 createmany -o $DIR/$tdir/$tfile-2- 25
    do_node $CLIENT1 createmany -o $DIR/$tfile-3- 25
    vbr_deactivate_client $CLIENT2

    facet_failover $SINGLEMDS
    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 4

    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 1

    # All files should have been replayed
    do_node $CLIENT1 unlinkmany $DIR/$tfile- 25 || return 2
    do_node $CLIENT1 unlinkmany $DIR/$tfile-3- 25 || return 3
    do_node $CLIENT2 unlinkmany $DIR/$tdir/$tfile-2- 25 || return 5

    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 7a "fail MDS, delayed recovery, fail MDS"

test_8a() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    rmultiop_start $CLIENT2 $DIR/$tfile O_tSc || return 1
    do_node $CLIENT2 rm -f $DIR/$tfile
    replay_barrier $SINGLEMDS
    rmultiop_stop $CLIENT2 || return 2

    vbr_deactivate_client $CLIENT2
    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 3
    #client1 is back and will try to open orphan
    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 4

    do_node $CLIENT2 $CHECKSTAT $DIR/$tfile && error "$tfile exists"
    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 8a "orphans are kept until delayed recovery"

test_8b() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    rmultiop_start $CLIENT2 $DIR/$tfile O_tSc|| return 1
    replay_barrier $SINGLEMDS
    do_node $CLIENT1 rm -f $DIR/$tfile

    vbr_deactivate_client $CLIENT2
    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 2
    #client1 is back and will try to open orphan
    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 3

    rmultiop_stop $CLIENT2 || return 1
    do_node $CLIENT2 $CHECKSTAT $DIR/$tfile && error "$tfile exists"
    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 8b "open1 | unlink2 X delayed_replay1, close1"

test_8c() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    rmultiop_start $CLIENT2 $DIR/$tfile O_tSc|| return 1
    replay_barrier $SINGLEMDS
    do_node $CLIENT1 rm -f $DIR/$tfile
    rmultiop_stop $CLIENT2 || return 2

    vbr_deactivate_client $CLIENT2
    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 3
    #client1 is back and will try to open orphan
    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 4

    do_node $CLIENT2 $CHECKSTAT $DIR/$tfile && error "$tfile exists"
    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 8c "open1 | unlink2, close1 X delayed_replay1"

test_8d() {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    remote_server $CLIENT2 || \
        { skip_env "Client $CLIENT2 is on the server node" && return 0; }

    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    rmultiop_start $CLIENT1 $DIR/$tfile O_tSc|| return 1
    rmultiop_start $CLIENT2 $DIR/$tfile O_tSc|| return 2
    replay_barrier $SINGLEMDS
    do_node $CLIENT1 rm -f $DIR/$tfile
    rmultiop_stop $CLIENT2 || return 3
    rmultiop_stop $CLIENT1 || return 4

    vbr_deactivate_client $CLIENT2
    facet_failover $SINGLEMDS
    client_up $CLIENT1 || return 6

    #client1 is back and will try to open orphan
    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 8

    do_node $CLIENT2 $CHECKSTAT $DIR/$tfile && error "$tfile exists"
    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 8d "open1, open2 | unlink2, close1, close2 X delayed_replay1"

test_8e() {
    zconf_mount $CLIENT1 $DIR
    zconf_mount $CLIENT2 $DIR

    do_node $CLIENT1 mcreate $DIR/$tfile
    do_node $CLIENT1 mkdir $DIR/$tfile-2
    replay_barrier $SINGLEMDS
    # missed replay from client1 will lead to recovery by versions
    do_node $CLIENT1 touch $DIR/$tfile-2/$tfile
    do_node $CLIENT2 rm $DIR/$tfile || return 1
    do_node $CLIENT2 touch $DIR/$tfile || return 2

    zconf_umount $CLIENT1 $DIR
    facet_failover $SINGLEMDS
    client_up $CLIENT2 || return 6

    do_node $CLIENT2 rm $DIR/$tfile || error "$tfile doesn't exists"
    zconf_umount_clients $CLIENTS $DIR
    return 0
}
run_test 8e "create | unlink, create shouldn't fail"

test_8f() {
    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    do_node $CLIENT1 touch $DIR/$tfile
    do_node $CLIENT1 mkdir $DIR/$tfile-2
    replay_barrier $SINGLEMDS
    # missed replay from client1 will lead to recovery by versions
    do_node $CLIENT1 touch $DIR/$tfile-2/$tfile
    do_node $CLIENT2 rm -f $DIR/$tfile || return 1
    do_node $CLIENT2 mcreate $DIR/$tfile || return 2

    zconf_umount $CLIENT1 $DIR
    facet_failover $SINGLEMDS
    client_up $CLIENT2 || return 6

    do_node $CLIENT2 rm $DIR/$tfile || error "$tfile doesn't exists"
    zconf_umount $CLIENT2 $DIR
    return 0
}
run_test 8f "create | unlink, create shouldn't fail"

test_8g() {
    zconf_mount_clients $CLIENT1 $DIR
    zconf_mount_clients $CLIENT2 $DIR

    do_node $CLIENT1 touch $DIR/$tfile
    do_node $CLIENT1 mkdir $DIR/$tfile-2
    replay_barrier $SINGLEMDS
    # missed replay from client1 will lead to recovery by versions
    do_node $CLIENT1 touch $DIR/$tfile-2/$tfile
    do_node $CLIENT2 rm -f $DIR/$tfile || return 1
    do_node $CLIENT2 mkdir $DIR/$tfile || return 2

    zconf_umount $CLIENT1 $DIR
    facet_failover $SINGLEMDS
    client_up $CLIENT2 || return 6

    do_node $CLIENT2 rmdir $DIR/$tfile || error "$tfile doesn't exists"
    zconf_umount $CLIENT2 $DIR
    return 0
}
run_test 8g "create | unlink, create shouldn't fail"

test_10 () {
    delayed_recovery_enabled || { skip "No delayed recovery support"; return 0; }

    [ -z "$DBENCH_LIB" ] && skip_env "DBENCH_LIB is not set" && return 0

    zconf_mount_clients $CLIENTS $DIR

    local duration="-t 60"
    local cmd="rundbench 1 $duration "
    local PID=""
    for CLIENT in ${CLIENTS//,/ }; do
        $PDSH $CLIENT "set -x; PATH=:$PATH:$LUSTRE/utils:$LUSTRE/tests/:${DBENCH_LIB} DBENCH_LIB=${DBENCH_LIB} $cmd" &
        PID=$!
        echo $PID >pid.$CLIENT
        echo "Started load PID=`cat pid.$CLIENT`"
    done

    replay_barrier $SINGLEMDS
    sleep 3 # give clients a time to do operations

    vbr_deactivate_client $CLIENT2

    log "$TESTNAME fail $SINGLEMDS 1"
    fail $SINGLEMDS

# wait for client to reconnect to MDS
    sleep $TIMEOUT

    vbr_activate_client $CLIENT2
    client_up $CLIENT2 || return 4

    for CLIENT in ${CLIENTS//,/ }; do
        PID=`cat pid.$CLIENT`
        wait $PID
        rc=$?
        echo "load on ${CLIENT} returned $rc"
    done

    zconf_umount_clients $CLIENTS $DIR
}
run_test 10 "mds version recovery; $CLIENTCOUNT clients"

[ "$CLIENTS" ] && zconf_mount_clients $CLIENTS $DIR

equals_msg `basename $0`: test complete, cleaning up
check_and_cleanup_lustre
[ -f "$TESTSUITELOG" ] && cat $TESTSUITELOG && grep -q FAIL $TESTSUITELOG && exit 1 || true
