#! /usr/bin/env bash

loops=10000
busywait=-b

peer=mora2
iface=ix0
piface=$iface
build=~/warpcore/freebsd-rel

ssh="ssh $peer -q"

run () {
    echo "run $1"
    $ssh "sudo pkill -f inetd"
    $ssh "sh -c '(cd $build/.. && \
            sudo nohup $build/examples/$1inetd -i $piface $busywait ) \
            > $build/../$1inetd.log 2>&1 &'"
    "$build/examples/$1ping" -i $iface -d "$peerip" -l $loops $busywait -e 4096 \
        >> "$1.txt" 2> "$1ping.log"
    $ssh "sudo pkill -f inetd"
}

sudo rm $build/../shim* $build/../warp*
# > /dev/null 2>&1

sudo pkill -f "dhclient: $iface"
sudo ifconfig $iface 10.11.12.3/24

peerip=10.11.12.4
$ssh "sudo pkill -f \"dhclient: $piface\""
$ssh "sudo ifconfig $piface $peerip/24"

run shim
sleep 5
run warp

sudo ifconfig $iface -alias 10.11.12.3
$ssh "sudo ifconfig $piface -alias $peerip"
