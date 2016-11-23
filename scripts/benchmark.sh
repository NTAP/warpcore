#! /usr/bin/env bash

set -e

loops=10000
busywait=-b

peer=four
iface=ix0
piface=ix0
build=~/warpcore/freebsd-rel

ssh="ssh $peer -q"

peerip=$(
    $ssh "/sbin/ifconfig $piface" | \
        sed -e s/addr:// -e s/^[[:space:]]*//g | \
        grep 'inet ' | cut -f 2 -d' '
)

run () {
    $build/examples/$1ping -i $iface -d $peerip -l $loops $busywait \
        >> "$1.txt" 2> "$1ping.log"
}


if [ -z "$(/sbin/ifconfig $iface | grep 'inet ')" ]; then
    echo local interface has no IP address
    exit
fi

if [ -z "$peerip" ]; then
    echo remote interface has no IP address
    exit
fi

rm warp*.txt shim*.txt ./*.log > /dev/null 2>&1 || true

$ssh "pkill -f warpinetd" || true
$ssh "(cd $build && nohup examples/warpinetd -i $piface $busywait ) \
        > warpinetd.log 2>&1 &" || true
run warp
$ssh "pkill -f warpinetd" || true

sleep 3
run shim
