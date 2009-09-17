#!/bin/bash
#
# Test basic functionality of the filesystem using simple
# benchmarks.
#

set -e

LUSTRE=${LUSTRE:-$(cd $(dirname $0)/..; echo $PWD)}
. $LUSTRE/tests/test-framework.sh
init_test_env $@
. ${CONFIG:=$LUSTRE/tests/cfg/$NAME.sh}

MAX_THREADS=${MAX_THREADS:-20}
RAMKB=`awk '/MemTotal:/ { print $2 }' /proc/meminfo`
if [ -z "$THREADS" ]; then
	THREADS=$((RAMKB / 16384))
	[ $THREADS -gt $MAX_THREADS ] && THREADS=$MAX_THREADS
fi
SIZE=${SIZE:-$((RAMKB * 2))}
RSIZE=${RSIZE:-512}

DEBUG_LVL=${DEBUG_LVL:-0}
DEBUG_OFF=${DEBUG_OFF:-"eval lctl set_param debug=\"$DEBUG_LVL\""}
DEBUG_ON=${DEBUG_ON:-"eval lctl set_param debug=0x33f0484"}

[ "$SLOW" = "no" ] && EXCEPT_SLOW="iozone"


build_test_filter
check_and_setup_lustre

test_dbench() {
    if ! which dbench > /dev/null 2>&1 ; then
	skip "No dbench installed"
	return
    fi

    DBENCHDIR=$MOUNT/d0.$HOSTNAME
    mkdir -p $DBENCHDIR
    local SPACE=`df -P $MOUNT | tail -n 1 | awk '{ print $4 }'`
    DB_THREADS=$((SPACE / 50000))
    [ $THREADS -lt $DB_THREADS ] && DB_THREADS=$THREADS
    
    $DEBUG_OFF
    myUID=$RUNAS_ID
    myRUNAS=$RUNAS
    FAIL_ON_ERROR=false check_runas_id_ret $myUID $myUID $myRUNAS || { myRUNAS="" && myUID=$UID; }
    chown $myUID:$myUID $DBENCHDIR
    local duration=""
    [ "$SLOW" = "no" ] && duration=" -t 120"
    if [ "$SLOW" != "no" -o $DB_THREADS -eq 1 ]; then
	$myRUNAS bash rundbench -D $DBENCHDIR 1 $duration || error "dbench failed!"
	$DEBUG_ON
    fi
    if [ $DB_THREADS -gt 1 ]; then
	$DEBUG_OFF
	$myRUNAS bash rundbench -D $DBENCHDIR $DB_THREADS $duration
	$DEBUG_ON
    fi
    rm -rf $DBENCHDIR
}
run_test dbench "dbench"

test_bonnie() {
    if ! which bonnie++ > /dev/null 2>&1; then
	skip "No bonnie++ installed"
	return 0
    fi
    BONDIR=$MOUNT/d0.bonnie
    mkdir -p $BONDIR
    $LFS setstripe -c -1 $BONDIR
    sync
    local MIN=`lctl get_param -n osc.*.kbytesavail | sort -n | head -n1`
    local SPACE=$(( OSTCOUNT * MIN ))
    [ $SPACE -lt $SIZE ] && SIZE=$((SPACE * 3 / 4))
    log "min OST has ${MIN}kB available, using ${SIZE}kB file size"
    $DEBUG_OFF
    myUID=$RUNAS_ID
    myRUNAS=$RUNAS
    FAIL_ON_ERROR=false check_runas_id_ret $myUID $myUID $myRUNAS || { myRUNAS="" && myUID=$UID; }
    chown $myUID:$myUID $BONDIR		
    $myRUNAS bonnie++ -f -r 0 -s$((SIZE / 1024)) -n 10 -u$myUID:$myUID -d$BONDIR
    $DEBUG_ON
}
run_test bonnie "bonnie++"

test_iozone() {
    if ! which iozone > /dev/null 2>&1; then
	skip "No iozone installed"
	return 0
    fi

    export O_DIRECT
    
    IOZDIR=$MOUNT/d0.iozone
    mkdir -p $IOZDIR
    $LFS setstripe -c -1 $IOZDIR
    sync
    local MIN=`lctl get_param -n osc.*.kbytesavail | sort -n | head -n1`
    local SPACE=$(( OSTCOUNT * MIN ))
    [ $SPACE -lt $SIZE ] && SIZE=$((SPACE * 3 / 4))
    log "min OST has ${MIN}kB available, using ${SIZE}kB file size"
    IOZONE_OPTS="-i 0 -i 1 -i 2 -e -+d -r $RSIZE"
    IOZFILE="$IOZDIR/iozone"
    IOZLOG=$TMP/iozone.log
		# $SPACE was calculated with all OSTs
    $DEBUG_OFF
    myUID=$RUNAS_ID
    myRUNAS=$RUNAS
    FAIL_ON_ERROR=false check_runas_id_ret $myUID $myUID $myRUNAS || { myRUNAS="" && myUID=$UID; }
    chown $myUID:$myUID $IOZDIR
    $myRUNAS iozone $IOZONE_OPTS -s $SIZE -f $IOZFILE 2>&1 | tee $IOZLOG
    tail -1 $IOZLOG | grep -q complete || \
	{ error "iozone (1) failed" && return 1; }
    rm -f $IOZLOG
    $DEBUG_ON
    
    # check if O_DIRECT support is implemented in kernel
    if [ -z "$O_DIRECT" ]; then
	touch $MOUNT/f.iozone
	if ! ./directio write $MOUNT/f.iozone 0 1; then
	    log "SKIP iozone DIRECT IO test"
	    O_DIRECT=no
	fi
	rm -f $MOUNT/f.iozone
    fi
    if [ "$O_DIRECT" != "no" -a "$IOZONE_DIR" != "no" ]; then
	$DEBUG_OFF
	$myRUNAS iozone -I $IOZONE_OPTS -s $SIZE -f $IOZFILE.odir 2>&1 | tee $IOZLOG
	tail -1 $IOZLOG | grep -q complete || \
	    { error "iozone (2) failed" && return 1; }
	rm -f $IOZLOG
	$DEBUG_ON
    fi

    SPACE=`df -P $MOUNT | tail -n 1 | awk '{ print $4 }'`
    IOZ_THREADS=$((SPACE / SIZE * 2 / 3 ))
    [ $THREADS -lt $IOZ_THREADS ] && IOZ_THREADS=$THREADS
    IOZVER=`iozone -v | awk '/Revision:/ {print $3}' | tr -d .`
    if [ "$IOZ_THREADS" -gt 1 -a "$IOZVER" -ge 3145 ]; then
	$LFS setstripe -c -1 $IOZDIR
	$DEBUG_OFF
	THREAD=1
	IOZFILE=" "
	while [ $THREAD -le $IOZ_THREADS ]; do
	    IOZFILE="$IOZFILE $IOZDIR/iozone.$THREAD"
	    THREAD=$((THREAD + 1))
	done
	$myRUNAS iozone $IOZONE_OPTS -s $((SIZE / IOZ_THREADS)) -t $IOZ_THREADS -F $IOZFILE 2>&1 | tee $IOZLOG
	tail -1 $IOZLOG | grep -q complete || \
	    { error "iozone (3) failed" && return 1; }
	rm -f $IOZLOG
	$DEBUG_ON
    elif [ $IOZVER -lt 3145 ]; then
	VER=`iozone -v | awk '/Revision:/ { print $3 }'`
	echo "iozone $VER too old for multi-thread test"
    fi
}
run_test iozone "iozone"

test_fsx() {
    FSX_SIZE=$SIZE
    FSX_COUNT=1000
    local SPACE=`df -P $MOUNT | tail -n 1 | awk '{ print $4 }'`
    [ $SPACE -lt $FSX_SIZE ] && FSX_SIZE=$((SPACE * 3 / 4))
    $DEBUG_OFF
    FSX_SEED=${FSX_SEED:-$RANDOM}
    rm -f $MOUNT/fsxfile
    $LFS setstripe -c -1 $MOUNT/fsxfile
    echo Using FSX_SEED=$FSX_SEED FSX_SIZE=$FSX_SIZE FSX_COUNT=$FSX_COUNT
    fsx -c 50 -p 1000 -S $FSX_SEED -P $TMP -l $FSX_SIZE \
	-N $(($FSX_COUNT * 100)) $MOUNT/fsxfile
    $DEBUG_ON
}
run_test fsx "fsx"

equals_msg `basename $0`: test complete, cleaning up
check_and_cleanup_lustre
[ -f "$TESTSUITELOG" ] && cat $TESTSUITELOG && grep -q FAIL $TESTSUITELOG && exit 1 || true