#! /usr/bin/env bash

set -e

warpcore=/home/elars/warpcore

declare -A tests=(
    # [tag]=speed:iter:clnt:clnt_if:clnt_ip:serv:serv_if:serv_ip
    # [freebsd1]=1:10:mora1:igb1:10.11.12.7:mora2:igb1:10.11.12.8
    # [freebsd10]=10:50:mora1:ix0:10.11.12.7:mora2:ix0:10.11.12.8
    # [freebsd40]=40:100:mora1:vcc0:10.11.12.7:mora2:vcc0:10.11.12.8
    # [freebsd100]=1:200:mora1:vcc1:10.11.12.7:mora2:vcc1:10.11.12.8

    # [linux1]=1:10:mora1:eno3:10.11.12.7:mora2:eno3:10.11.12.8
    # [linux10]=10:50:mora1:enp2s0f0:10.11.12.7:mora2:enp2s0f0:10.11.12.8
    [linux40]=40:100:mora1:enp6s0f0:10.11.12.7:mora2:enp6s0f0:10.11.12.8
    # [linux100]=1:200:mora1:vcc1:10.11.12.7:mora2:vcc1:10.11.12.8
)

speed=0
iter=1
clnt=2
clnt_if=3
clnt_ip=4
serv=5
serv_if=6
serv_ip=7


declare -A pin=(
    [Linux]="/usr/bin/taskset -c 3"
    [FreeBSD]="/usr/bin/cpuset -l 3"
)

declare -A os


red=$(printf '\033[31m')
grn=$(printf '\033[32m')
blu=$(printf '\033[34m')
bld=$(printf '\033[1m')
nrm=$(printf '\033[0m')


function run() {
    local dst=$1
    local cmd=$2
    (echo "${grn}${bld}[$dst]${nrm}${blu} $cmd${nrm}" | tr -s " " >&2)
    ssh "$dst" "bash -c \"$cmd\""
}


function ip_unconf() {
    local role=$1
    shift
    local t=("$@")
    local host_if=${t[${role}_if]}
    local host_ip=${t[${role}_ip]}
    echo $host_ip

    if [ "${os[$role]}" == Linux ]; then
        cmd=""
        for i in $(run "${t[$role]}" "ls /sys/class/net"); do
            cmd="$cmd sudo ip addr del $host_ip/24 dev $i & "
        done
    else
        for i in $(run "${t[$role]}" "ifconfig -l"); do
            cmd="$cmd sudo ifconfig $i -alias $host_ip 2> /dev/null & "
        done
    fi
    run "${t[$role]}" "$cmd wait"
}


function ip_conf() {
    local role=$1
    shift
    local t=("$@")
    local host_if=${t[${role}_if]}
    local host_ip=${t[${role}_ip]}

    if [ "${os[$role]}" == Linux ]; then
        cmd="sudo sysctl net.core.rmem_max=26214400 \
                         net.core.wmem_max=26214400 \
                         net.core.rmem_default=26214400 \
                         net.core.wmem_default=26214400; \
             sudo ifconfig $host_if down; \
             sudo ethtool -C $host_if adaptive-rx off adaptive-tx off \
                 rx-usecs 0 tx-usecs 0; \
             sudo ethtool -C $host_if rx-frames-irq 1 tx-frames-irq 1; \
             sudo ethtool -G $host_if rx 512 tx 512; \
             sudo ethtool -A $host_if rx off tx off; \
             sudo ethtool -L $host_if combined 2; \
             sudo ethtool --set-eee $host_if eee off"
    else
        cmd="sudo pkill -f 'dhclient: $host_if'"
    fi
    cmd="$cmd; sudo ifconfig $host_if $host_ip/24 up"
    run "${t[$role]}" "$cmd"
}


function build() {
    local role=$1
    shift
    local t=("$@")

    [ -n "$built" ] && return
    echo "${red}Building ${t[$role]}/${os[$role]}${nrm}"
    run "${t[$role]}" "\
        mkdir -p $warpcore/${os[$role]}-benchmarking; \
        cd $warpcore/${os[$role]}-benchmarking; \
        git pull --recurse-submodules; \
        cmake -DBENCHMARKING=1 -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo ..; \
        ninja"
    built=1
}


function stop() {
    local role=$1
    shift
    local t=("$@")
    run "${t[$role]}" "sudo pkill '(warp|sock)(ping|inetd)'" || true
}


function netmap_unconf() {
    local role=$1
    shift
    local t=("$@")
    local host_if=${t[${role}_if]}

    if [ "${os[$role]}" == Linux ]; then
        cmd="sudo ifconfig $host_if down; \
             sudo ethtool -K $host_if sg on rx on tx on tso on gro on lro on; \
             sudo ethtool -C $host_if \
                adaptive-rx on adaptive-tx on rx-usecs 10 ; \
             sudo ifconfig $host_if up"
    else
        cmd="sudo ifconfig $host_if rxcsum txcsum tso lro"
    fi
    run "${t[$role]}" "$cmd"
}


function netmap_conf() {
    local role=$1
    shift
    local t=("$@")
    local host_if=${t[${role}_if]}

    if [ "${os[$role]}" == Linux ]; then
        cmd="cd /sys/module/netmap/parameters; \
             echo 4096 | sudo tee if_size > /dev/null; \
             echo 1 | sudo tee admode > /dev/null; \
             echo 1000000 | sudo tee buf_num > /dev/null; \
             sudo ifconfig $host_if down; \
             sudo ethtool -K $host_if sg off rx off tx off tso off \
                gro off lro off;\
             sudo ifconfig $host_if up"
    else
        cmd="sudo sysctl dev.netmap.if_size=4096; \
             sudo sysctl dev.netmap.admode=1; \
             sudo sysctl dev.netmap.buf_num=1000000; \
             sudo ifconfig $host_if -rxcsum -txcsum -tso -lro"
    fi
    run "${t[$role]}" "$cmd"

}


function start_clnt() {
    local busywait=$1
    local cksum=$2
    local kind=$3
    shift 3
    local t=("$@")

    prefix="../${kind}ping-${t[$speed]}${busywait}${cksum}"
    file="${prefix}.txt"
    log="${prefix}.log"
    run "${t[$clnt]}" "\
        cd $warpcore/${os[clnt]}-benchmarking; \
        ${pin[${os[clnt]}]} bin/${kind}ping -i ${t[$clnt_if]} \
            -d ${t[$serv_ip]} $busywait $cksum -l ${t[$iter]} \
            -s 32 -p 0 -e 17000000 > $file 2> $log"
}


function start_serv() {
    local busywait=$1
    local cksum=$2
    local kind=$3
    shift 3
    local t=("$@")

    prefix="../${kind}inetd-${t[$speed]}${busywait}${cksum}"
    file="${prefix}.txt"
    log="${prefix}.log"
    run "${t[$serv]}" "\
        cd $warpcore/${os[serv]}-benchmarking; \
        /usr/bin/nohup ${pin[${os[serv]}]} bin/${kind}inetd -i ${t[$serv_if]} \
        $busywait $cksum > /dev/null 2> $log &"
}


function clean_logs() {
    local t=("$@")

    run "${t[clnt]}" "\
        cd $warpcore; \
        rm warp*.log sock*.log warp*.txt sock*.txt warp*.prof sock*.prof" \
    || true
}


for tag in "${!tests[@]}"; do
    IFS=':' read -ra t <<< "${tests[$tag]}"

    # set host OS
    [ -z "${os[clnt]}" ] && os[clnt]=$(run "${t[clnt]}" "uname -s")
    [ -z "${os[serv]}" ] && os[serv]=$(run "${t[serv]}" "uname -s")

    echo "${red}Baseline config${nrm}"
    for h in clnt serv; do
        stop $h "${t[@]}"
        netmap_unconf $h "${t[@]}"
        ip_unconf $h "${t[@]}"
        ip_conf $h "${t[@]}"
    done

    [ -n "$1" ] && exit

    build clnt "${t[@]}"
    [ "${os[clnt]}" != "${os[serv]}" ] && build serv "${t[@]}"

    clean_logs "${t[@]}"
    for k in sock warp; do
        if [ $k == warp ]; then
            echo "${red}netmap config${nrm}"
            netmap_conf clnt "${t[@]}" &
            netmap_conf serv "${t[@]}" &
            wait
        fi

        for c in -z ""; do
            for w in -b ""; do
                echo "${red}Benchmark $t $k $c $w${nrm}"
                start_serv "$w" "$c" "$k" "${t[@]}"
                sleep 3
                start_clnt "$w" "$c" "$k" "${t[@]}"
                stop clnt "${t[@]}" &
                stop serv "${t[@]}" &
                wait
            done
        done

        if [ $k == warp ]; then
            echo "${red}Undo netmap config${nrm}"
            netmap_unconf clnt "${t[@]}" &
            netmap_unconf serv "${t[@]}" &
            wait
        fi
    done

    echo "${red}Undo config${nrm}"
    ip_unconf $h "${t[@]}" clnt &
    ip_unconf $h "${t[@]}" serv &
    wait
done
