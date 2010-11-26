#!/bin/bash
#
# This test is used to compare the parameters from /proc and params_tree,
# also used in sanity.sh (test_217). 

LUSTRE=${LUSTRE:-$(cd $(dirname $0)/..; echo $PWD)}
LCTL=${LCTL:-"$LUSTRE/utils/lctl"}
[ ! -f $LCTL ] && LCTL=`which lctl 2>/dev/null`
difflog=$1

#get params from /proc and params_tree
rm -f $difflog
touch $difflog
read_error=0
write_error=0
output=/tmp/write_error.log
rm -f $output
touch $output
proc_dirs="/proc/fs/lustre /proc/sys/lustre /proc/fs/lnet /proc/sys/lnet"
for dir in $proc_dirs; do 
        [ -d $dir ] || continue
        cd $dir > /dev/null
        proc_files=`find`
        for path in $proc_files; do
                [ -d $path ] && continue
                param=${path:2}
                #e.g. mgc.MGC10\.10\.121\.1@tcp.import
                new=`echo $param | awk 'gsub(/\./,"\\\\.")'`
                [ ! -z $new ] && param=$new
                ptreep=`$LCTL get_param -n "$param" 2>/dev/null`
                procp=`cat $path 2>/dev/null`

                #compare parameter value
                if [ "$ptreep" != "$procp" ]; then
                        len=`expr length $param`
                        #Exception: including snapshot_time
                        [ "${param:len-5}" == "stats" ] && continue
                        [ "${param:len-4}" == "time" ] && continue
                        [ "${param:len-9}" == "/timeouts" ] && continue
                        [ "${param:len-18}" == "encrypt_page_pools" ] && continue
                        #Exception: change everytime
                        [ "${param:len-7}" == "memused" ] && continue
                        #Exception: change with obd_timeout
                        [ "$param" == "ldlm_timeout" ] && continue

                        echo "$dir/$param "
                        echo -e "from proc:\n$procp"
                        echo -e "from ptree:\n$ptreep\n"
                        let read_error=$read_error+1
                else #test write
                        mode=`$LCTL list_param -F $param | awk '{ print substr($1,length($1)) }'`
                        [ "$mode" != "=" ] && continue
                        [ `echo "$ptreep" | wc -l` -ne 1 ] && continue
                        [ `echo "$ptreep" | awk '{ print NF }'` -ne 1 ] && continue

                        $LCTL set_param -n "$param"="$ptreep" 2>$output
                        [ `echo $output | grep "error: set_param" | wc -l` -gt 0 ] || continue
                        #Exception: can't be set with value <= 0
                        [ `echo $output | grep "pool\.limit" | wc -l` -gt 0 ] && continue
                        #Exception: can't be set with value >= lqc_bunit_sz
                        [ `echo $output | grep "quota_btune_sz" | wc -l` -gt 0 ] && continue

                        echo "$output"
                        let write_error=$write_error+1
                fi
        done
        cd - > /dev/null
done
rm -f $output
#summary for read error
[ $read_error -gt 0 ] && echo "total $read_error different parameters found from `hostname`" >> $difflog
#summary for write error
[ $write_error -gt 0 ] && echo "total $write_error write errors found from `hostname`" >> $difflog
