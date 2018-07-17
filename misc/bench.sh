#! /usr/bin/env bash

set -e

warpcore=/home/elars/warpcore

declare -A -x test1=(
        [speed]=1
        [iter]=10
        [clnt]=mora1
        [serv]=mora2
        # [clnt_if]=igb1
        # [serv_if]=igb1
        [clnt_if]=eno3
        [serv_if]=eno3
        [clnt_ip]=10.11.12.7
        [serv_ip]=10.11.12.8
)

declare -A -x test10=(
        [speed]=10
        [iter]=50
        [clnt]=mora1
        [serv]=mora2
        # [clnt_if]=ix0
        # [serv_if]=ix0
        [clnt_if]=enp2s0f0
        [serv_if]=enp2s0f0
        [clnt_ip]=10.11.12.7
        [serv_ip]=10.11.12.8
)

declare -A -x test40=(
        [speed]=40
        [iter]=100
        [clnt]=mora1
        [serv]=mora2
        [clnt_if]=ixl0
        [serv_if]=ixl0
        # [clnt_if]=enp6s0f0
        # [serv_if]=enp6s0f0
        [clnt_ip]=10.11.12.7
        [serv_ip]=10.11.12.8
)

tests=(test40)


declare -A pin=(
        [Linux]="/usr/bin/taskset -c 3"
        [FreeBSD]="/usr/bin/cpuset -l 3"
)


red=$(tput setaf 1)
green=$(tput setaf 2)
blue=$(tput setaf 4)
bold=$(tput bold)
norm=$(tput sgr0)


function run() {
        local dst=$1
        local cmd=$2
        (echo "${green}${bold}[$dst]${norm}${blue} $cmd${norm}" | tr -s " " >&2)
        ssh "$dst" "bash -c \"$cmd\""
}


function ip_unconf() {
        local t=$1
        declare -n host="${t}[$2]"
        declare -n host_if="${t}[$2_if]"
        declare -n host_ip="${t}[$2_ip]"
        declare -n host_os="${host}[os]"
        if [ "$host_os" == Linux ]; then
                cmd="sudo ethtool --set-channels $host_if combined 10;"
                for i in $(run "$host" "ls /sys/class/net"); do
                        cmd="$cmd sudo ip addr del $host_ip/24 dev $i &"
                done
        else
                for i in $(run "$host" "ifconfig -l"); do
                        cmd="$cmd sudo ifconfig $i -alias $host_ip &"
                done
        fi
        run "$host" "$cmd wait"
}


function ip_conf() {
        local t=$1
        declare -n host="${t}[$2]"
        declare -n host_if="${t}[$2_if]"
        declare -n host_ip="${t}[$2_ip]"
        declare -n host_os="${host}[os]"
        if [ "$host_os" == Linux ]; then
                cmd="sudo sysctl net.core.rmem_max=26214400 \
                             net.core.wmem_max=26214400 \
                             net.core.rmem_default=26214400 \
                             net.core.wmem_default=26214400; \
                     sudo ethtool --set-channels $host_if combined 2; \
                     sudo ethtool --set-eee $host_if eee off; \
                     sudo ethtool --pause $host_if \
                             autoneg off rx off tx off; \
                     sudo ethtool --coalesce $host_if \
                             adaptive-rx off adaptive-tx off \
                             rx-usecs 0 tx-usecs 0 \
                             rx-frames-irq 1 tx-frames-irq 1"
        else
                cmd="sudo pkill -f 'dhclient: $host_if'"
        fi
        cmd="$cmd; sudo ifconfig $host_if $host_ip/24 up"
        run "$host" "$cmd"
}


function build() {
        local t=$1
        declare -n host="${t}[$2]"
        declare -n host_os="${host}[os]"
        declare -n built="${host}[built]"
        [ -n "$built" ] && return
        echo "${red}Building $host/$host_os${norm}"
        run "$host" "mkdir -p $warpcore/$os-benchmarking; \
                     cd $warpcore/$os-benchmarking; \
                     git pull; \
                     cmake -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo ..; \
                     ninja"
        built=1
}


function stop() {
        local t=$1
        declare -n host="${t}[$2]"
        run "$host" "pkill '(warp|sock)(ping|inetd)'" || true
}


function netmap_unconf() {
        local t=$1
        declare -n host="${t}[$2]"
        declare -n host_if="${t}[$2_if]"
        declare -n host_os="${host}[os]"
        if [ "$host_os" == Linux ]; then
                cmd="sudo ethtool --features $host_if \
                             sg on rx on tx on tso on gro on lro on"
        else
                cmd="sudo ifconfig $host_if rxcsum txcsum tso lro"
        fi
        run "$host" "$cmd"
}


function netmap_conf() {
        local t=$1
        declare -n host="${t}[$2]"
        declare -n host_if="${t}[$2_if]"
        declare -n host_os="${host}[os]"
        if [ "$host_os" == Linux ]; then
                cmd="cd /sys/module/netmap/parameters; \
                     echo 4096 | sudo tee if_size > /dev/null; \
                     echo 1 | sudo tee admode > /dev/null; \
                     echo 1000000 | sudo tee buf_num > /dev/null; \
                     sudo ethtool --features $host_if \
                             sg off rx off tx off tso off gro off lro off"
        else
                cmd="sudo sysctl dev.netmap.if_size=4096; \
                     sudo sysctl dev.netmap.admode=1; \
                     sudo sysctl dev.netmap.buf_num=1000000; \
                     sudo ifconfig $host_if -rxcsum -txcsum -tso -lro"
        fi
        run "$host" "$cmd"

}


function start_clnt() {
        local t=$1
        local busywait=$2
        local cksum=$3
        local kind=$4
        declare -n clnt="${t}[clnt]"
        declare -n speed="${t}[speed]"
        declare -n clnt_if="${t}[clnt_if]"
        declare -n serv_ip="${t}[serv_ip]"
        declare -n iter="${t}[iter]"
        declare -n clnt_os="${clnt}[os]"

        prefix="../${kind}ping-${speed}${busywait}${cksum}"
        file="${prefix}.txt"
        log="${prefix}.log"
        run "$clnt" "cd $warpcore/$os-benchmarking; \
                     rm -f $file $log; \
                     ${pin[$clnt_os]} bin/${kind}ping \
                             -i $clnt_if -d $serv_ip $busywait $cksum -l $iter \
                             -s 32 -p 0 -e 17000000 > $file 2> $log"
}


function start_serv() {
        local t=$1
        local busywait=$2
        local cksum=$3
        local kind=$4
        declare -n serv="${t}[serv]"
        declare -n speed="${t}[speed]"
        declare -n serv_if="${t}[serv_if]"
        declare -n serv_os="${serv}[os]"

        prefix="../${kind}inetd-${speed}${busywait}${cksum}"
        file="${prefix}.txt"
        log="${prefix}.log"
        run "$serv" "cd $warpcore/$os-benchmarking; \
                     rm -f $log; \
                     /usr/bin/nohup ${pin[$serv_os]} bin/${kind}inetd \
                             -i $serv_if $busywait $cksum \
                     < /dev/null > /dev/null 2> $log &"
}


function clean_logs() {
        local t=$1
        local busywait=$2
        local cksum=$3
        local kind=$4
        declare -n serv="${t}[serv]"
        declare -n speed="${t}[speed]"

        clnt_log="${kind}ping-${speed}${busywait}${cksum}.log"
        serv_log="${kind}inetd-${speed}${busywait}${cksum}.log"
        run "$serv" "cd $warpcore; \
                     [ ! -s $clnt_log ] && rm -f $clnt_log; \
                     [ ! -s $serv_log ] && rm -f $serv_log;"
}


for t in "${tests[@]}"; do
        declare -n tt="$t"
        declare -n clnt=${tt[clnt]}
        declare -n serv=${tt[serv]}

        for h in clnt serv; do
                # define per-host hashes
                declare -n host="${tt[$h]}"
                declare -A "${!host}"

                # set host OS
                declare -n os="${!host}[os]"
                [ -z "$os" ] && os=$(run "${!host}" "uname -s")
        done

        echo "${red}Baseline config${norm}"
        for h in clnt serv; do
                stop "$t" $h &
                netmap_unconf "$t" $h &
                ip_unconf "$t" $h &
        done
        wait
        ip_conf "$t" clnt &
        ip_conf "$t" serv &
        wait

        [ ! -z "$1" ] && exit

        declare -n clnt_os="${!clnt}[os]"
        declare -n serv_os="${!serv}[os]"
        build "$t" clnt
        [ "$clnt_os" != "$serv_os" ] && build "$t" serv

        for k in warp sock; do
                if [ $k == warp ]; then
                        echo "${red}netmap config${norm}"
                        netmap_conf "$t" clnt &
                        netmap_conf "$t" serv &
                        wait
                fi

                for c in -z ""; do
                        for w in -b ""; do
                                echo "${red}Benchmark $t $k $c $w${norm}"
                                start_serv "$t" "$w" "$c" "$k"
                                sleep 3
                                start_clnt "$t" "$w" "$c" "$k"
                                stop "$t" clnt &
                                stop "$t" serv &
                                wait
                                clean_logs "$t" "$w" "$c" "$k"
                        done
                done

                if [ $k == warp ]; then
                        echo "${red}Undo netmap config${norm}"
                        netmap_unconf "$t" clnt &
                        netmap_unconf "$t" serv &
                        wait
                fi
        done

        echo "${red}Undo config${norm}"
        ip_unconf "$t" clnt &
        ip_unconf "$t" serv &
        wait
done
