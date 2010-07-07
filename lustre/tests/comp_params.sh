#!/bin/bash
#
# This test is used to compare the parameters from /proc and params_tree,
# also used in sanity.sh (test_217). 

LUSTRE=${LUSTRE:-$(cd $(dirname $0)/..; echo $PWD)}
LCTL=${LCTL:-"$LUSTRE/utils/pthread/lctl"}
[ ! -f $LCTL ] && LCTL=`which lctl 2>/dev/null`
difflog=$1

#get params from /proc and params_tree
rm -f $difflog
touch $difflog
proc_dirs="/proc/fs/lustre /proc/sys/lustre /proc/fs/lnet /proc/sys/lnet"
count=0
for dir in $proc_dirs; do 
        [ -d $dir ] || continue
        cd $dir > /dev/null
        proc_files=`find`
        for path in $proc_files; do
                [ -d $path ] && continue
                param=${path:2}
                #To parse the parameter correctly, we need replace '.' with '#',
                #e.g. mgc.MGC10.10.121.1@tcp.import
                new=`echo $param | awk 'gsub(/\./,"#")'`
                [ ! -z $new ] && param=$new
                ptreep=`$LCTL get_param -n $param 2>/dev/null`
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
                        let count=$count+1;
                fi
        done
        cd - > /dev/null
done

[ $count -gt 0 ] && echo "total $count different parameters found from `hostname`" >> $difflog
