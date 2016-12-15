#! /usr/bin/env bash

loops=1
busywait=-b

peer=phobos2
# peer=three
iface=ix0
# iface=enp4s0f0 # 40G
# iface=enp8s0f0 # 10G
ip=10.11.12.3
piface=$iface
peerip=10.11.12.4

build=~/warpcore/$(uname -s)
#-rel

ssh="ssh $peer -q -M"

run () {
    echo "run $1"
    $ssh "sudo pkill -f inetd"
    $ssh "sh -c '(cd $build/.. && \
            sudo nohup $build/examples/$1inetd -i $piface $busywait ) \
            > $build/../$1inetd.log 2>&1 &'"
    # "$build/examples/$1ping" -i $iface -d "$peerip" -l $loops $busywait \
    #     >> "$1.txt" 2> "$1ping.log"
    "$build/examples/$1ping" -i $iface -d "$peerip" -l $loops $busywait \
        -s 3000000 -e 3000000 >> "$1.txt" 2> "$1ping.log"
    $ssh "sudo pkill -f inetd"
}


pushd "$build"; make || exit; popd
sudo rm "$build"/../shim* "$build"/../warp* > /dev/null 2>&1

sudo pkill -f "dhclient.*$iface"
sudo ifconfig $iface $ip/24 up
sudo ethtool -A $iface rx off tx off
sudo sysctl -q -w hw.ix.enable_aim=0

$ssh "sudo pkill -f \"dhclient.*$piface\""
$ssh "sudo ifconfig $piface $peerip/24 up"
$ssh "sudo ethtool -A $piface rx off tx off"
$ssh "sudo sysctl -q -w hw.ix.enable_aim=0"

if [ ! "$1" ]; then
        run shim
fi

sudo ifconfig $iface -rxcsum -txcsum -tso -lro
$ssh "sudo ifconfig $piface -rxcsum -txcsum -tso -lro"

if [ ! "$1" ]; then
        run warp
fi

if [ "$1" != "init" ]; then
    sudo ip addr del $ip/24 dev $iface
    sudo ifconfig $iface -alias 10.11.12.3 rxcsum txcsum tso lro
    $ssh "sudo ip addr del $peerip/24 dev $piface"
    $ssh "sudo ifconfig $piface -alias $peerip rxcsum txcsum tso lro"
fi
