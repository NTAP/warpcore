#! /usr/bin/env bash

set -e

warpcore=/home/elars/warpcore

declare -A test1=(
        [speed]=1
        [iter]=10
        [clnt]=mora1
        [serv]=mora2
        # [clnt_if]=igb1
        # [serv_if]=igb1
        [clnt_if]=eno3
        [serv_if]=eno3
        [clnt_ip]=10.11.12.1
        [serv_ip]=10.11.12.2
)

declare -A test10=(
        [speed]=10
        [iter]=50
        [clnt]=mora1
        [serv]=mora2
        # [clnt_if]=ix0
        # [serv_if]=ix0
        [clnt_if]=enp2s0f0
        [serv_if]=enp2s0f0
        [clnt_ip]=10.11.12.1
        [serv_ip]=10.11.12.2
)

declare -A test40=(
        [speed]=40
        [iter]=100
        [clnt]=mora1
        [serv]=mora2
        # [clnt_if]=ixl0
        # [serv_if]=ixl0
        [clnt_if]=enp6s0f0
        [serv_if]=enp6s0f0
        [clnt_ip]=10.11.12.1
        [serv_ip]=10.11.12.2
)

tests=(test1 test10 test40)


declare -A pin=(
        [Linux]="/usr/bin/taskset -c 3"
        [FreeBSD]="/usr/bin/cpuset -l 3"
)


function run() {
        local dst=$1
        local cmd=$2
        ssh "$dst" "bash -c \"$cmd\""
}


function ip_unconf() {
        local t=$1
        typeset -n host=$t[$2]
        typeset -n host_if=$t[$2_if]
        typeset -n host_ip=$t[$2_ip]
        typeset -n host_os=$host[os]
        cmd=""
        if [ "$host_os" == Linux ]; then
                for i in $(run "$host" "ls /sys/class/net"); do
                        [ -z "$host_ip" ] && exit 1
                        cmd="$cmd sudo ip addr del $host_ip/24 dev $i &"
                        cmd="$cmd sudo ethtool -G $i rx 512 tx 512 &"
                done
        else
                for i in $(run "$host" "ifconfig -l"); do
                        [ -z "$host_ip" ] && exit 1
                        cmd="$cmd sudo ifconfig $i -alias $host_ip"
                done
        fi
        run "$host" "$cmd & wait" 2> /dev/null || true
}


function ip_conf() {
        local t=$1
        typeset -n host=$t[$2]
        typeset -n host_if=$t[$2_if]
        typeset -n host_ip=$t[$2_ip]
        typeset -n host_os=$host[os]
        cmd=""
        if [ "$host_os" == Linux ]; then
                cmd="sudo ifconfig $host_if down; \
                     sudo sysctl net.core.rmem_max=26214400 & \
                     sudo sysctl net.core.wmem_max=26214400 & \
                     sudo sysctl net.core.rmem_default=26214400 & \
                     sudo sysctl net.core.wmem_default=26214400 & \
                     sudo ethtool -C $host_if adaptive-rx off adaptive-tx off \
                             rx-usecs 0 tx-usecs 0 & \
                     sudo ethtool -C $host_if rx-frames-irq 1 \
                             tx-frames-irq 1 & \
                     sudo ethtool -G $host_if rx 2048 tx 2048 & \
                     sudo ethtool -A $host_if rx off tx off & \
                     sudo ethtool -L $host_if combined 2 & \
                     sudo ethtool --set-eee $host_if eee off & wait; \
                     sudo ifconfig $host_if up"
        else
                cmd="sudo pkill -f 'dhclient: $host_if'"
        fi
        cmd="$cmd; sudo ifconfig $host_if $host_ip/24 up"
        run "$host" "$cmd" #2> /dev/null || true
}


function build() {
        local t=$1
        typeset -n host=$t[$2]
        typeset -n os=$host[os]
        run "$host" "mkdir -p $warpcore/$os-benchmarking; \
                     cd $warpcore/$os-benchmarking; \
                     cmake -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo ..; \
                     ninja"
}


function stop() {
        local t=$1
        typeset -n host=$t[$2]
        run "$host" "sudo pkill inetd; \
                     pkill '(warp|sock)(ping|inetd)'" || true
}


function netmap_unconf() {
        local t=$1
        typeset -n host=$t[$2]
        typeset -n host_if=$t[$2_if]
        typeset -n host_os=$host[os]
        cmd=""
        if [ "$host_os" == Linux ]; then
                cmd="sudo ifconfig $host_if down; \
                     sudo ethtool -K $host_if \
                             sg on rx on tx on tso on gro on lro on & \
                     sudo ethtool -C $host_if \
                             adaptive-rx on adaptive-tx on rx-usecs 10 & \
                     sudo ifconfig $host_if up"
        else
                cmd="sudo ifconfig $host_if rxcsum txcsum tso lro"
        fi
        run "$host" "$cmd & wait" #2> /dev/null || true
}


function netmap_conf() {
        local t=$1
        typeset -n host=$t[$2]
        typeset -n host_if=$t[$2_if]
        typeset -n host_os=$host[os]
        cmd=""
        if [ "$host_os" == Linux ]; then
                cmd="echo 4096 > \
                             sudo tee /sys/module/netmap/parameters/if_size & \
                     echo 1 > \
                             sudo tee /sys/module/netmap/parameters/admode & \
                     echo 1000000 > \
                             sudo tee /sys/module/netmap/parameters/buf_num & \
                     sudo ifconfig $host_if down; \
                     sudo ethtool -K $host_if sg off rx off tx off tso off \
                             gro off lro off;\
                     sudo ifconfig $host_if up"
        else
                cmd="sudo sysctl dev.netmap.if_size=4096 & \
                     sudo sysctl dev.netmap.admode=1 & \
                     sudo sysctl dev.netmap.buf_num=1000000 & \
                     sudo ifconfig $host_if -rxcsum -txcsum -tso -lro"
        fi
        run "$host" "$cmd & wait" #2> /dev/null || true

}


function start_clnt() {
        local t=$1
        local busywait=$2
        local cksum=$3
        local kind=$4
        typeset -n clnt=$t[clnt]
        typeset -n speed=$t[speed]
        typeset -n clnt_if=$t[clnt_if]
        typeset -n serv_ip=$t[serv_ip]
        typeset -n iter=$t[iter]
        typeset -n clnt_os=$clnt[os]

        prefix="../${kind}ping-${speed}${busywait}${cksum}"
        file="${prefix}.txt"
        log="${prefix}.log"
        run "$clnt" "cd $warpcore/$os-benchmarking; \
                     ${pin[$clnt_os]} bin/${kind}ping \
                             -i $clnt_if -d $serv_ip $busywait $cksum -l $iter \
                             s 32 -p 0 -e 17000000 > $file 2> $log"
}


function start_serv() {
        local t=$1
        local busywait=$2
        local cksum=$3
        local kind=$4
        typeset -n serv=$t[serv]
        typeset -n speed=$t[speed]
        typeset -n serv_if=$t[serv_if]
        typeset -n serv_os=$serv[os]

        prefix="../${kind}inetd-${speed}${busywait}${cksum}"
        file="${prefix}.txt"
        log="${prefix}.log"
        run "$serv" "cd $warpcore/$os-benchmarking; \
                     /usr/bin/nohup ${pin[$serv_os]} bin/${kind}inetd \
                             -i $serv_if $busywait $cksum \
                     < /dev/null > /dev/null 2> $log  &"
}


function clean_logs() {
        local t=$1
        local busywait=$2
        local cksum=$3
        local kind=$4
        typeset -n serv=$t[serv]
        typeset -n speed=$t[speed]

        clnt_log="${kind}ping-${speed}${busywait}${cksum}.log"
        serv_log="${kind}inetd-${speed}${busywait}${cksum}.log"
        run "$serv" "cd $warpcore; \
                     [ ! -s $clnt_log ] && rm $clnt_log; \
                     [ ! -s $serv_log ] && rm $serv_log;"
}


for t in "${tests[@]}"; do
        typeset -n clnt=$t[clnt]
        typeset -n serv=$t[serv]

        for h in clnt serv; do
                # define per-host hashes
                typeset -n host=$t[$h]
                declare -A "$host"

                # set host OS
                typeset -n os=$host[os]
                [ -z "$os" ] && os=$(run "$host" "uname -s")
        done

        echo "Baseline config"
        for h in clnt serv; do
                (stop "$t" $h; netmap_unconf "$t" $h) &
                (ip_unconf "$t" $h; ip_conf "$t" $h) &
        done
        wait

        [ ! -z "$1" ] && exit

        echo "Building"
        typeset -n clnt_os=$clnt[os]
        typeset -n serv_os=$serv[os]
        build "$t" clnt &
        [ "$clnt_os" != "$serv_os" ] && build "$t" serv &
        wait

        for k in warp sock; do
                if [ $k == warp ]; then
                        echo "netmap config"
                        netmap_conf "$t" clnt &
                        netmap_conf "$t" serv &
                        wait
                fi

                for c in "" -z; do
                        for w in -b ""; do
                                echo "Benchmark $k $c $w"
                                start_serv "$t" "$w" "$c" "$k"
                                sleep 1
                                start_clnt "$t" "$w" "$c" "$k"
                                stop "$t" clnt &
                                stop "$t" serv &
                                wait
                                clean_logs "$t" "$w" "$c" "$k"
                        done
                done

                if [ $k == warp ]; then
                        echo "Undo netmap config"
                        netmap_unconf "$t" clnt &
                        netmap_unconf "$t" serv &
                        wait
                fi
        done

        echo "Undo config"
        ip_unconf "$t" clnt &
        ip_unconf "$t" serv &
        wait
done
