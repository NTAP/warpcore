#! /usr/bin/env bash

loops=10000
busywait=-b

peer=phobos2
# iface=enp4s0f0 # 40G
iface=enp8s0f0 # 10G

piface=$iface
build=~/warpcore/$(uname -s)-rel

pushd "$build"; make; popd

ssh="ssh $peer -q"

run () {
    echo "run $1"
    $ssh "sudo pkill -f inetd"
    $ssh "sh -c '(cd $build/.. && \
            sudo nohup $build/examples/$1inetd -i $piface $busywait ) \
            > $build/../$1inetd.log 2>&1 &'"
    sleep 3
    "$build/examples/$1ping" -i $iface -d "$peerip" -l $loops $busywait \
        >> "$1.txt" 2> "$1ping.log"
    $ssh "sudo pkill -f inetd"
}

sudo rm "$build"/../shim* "$build"/../warp* > /dev/null 2>&1

sudo pkill -f "dhclient.*$iface"
sudo ifconfig $iface 10.11.12.3/24 up
sudo ethtool -A $iface rx off tx off

peerip=10.11.12.4
$ssh "sudo pkill -f \"dhclient.*$piface\""
$ssh "sudo ifconfig $piface $peerip/24 up"
$ssh "sudo ethtool -A $piface rx off tx off"

run shim
run warp

sudo ip addr del 10.11.12.3/24 dev $iface
$ssh "sudo ip addr del $peerip/24 dev $piface"
