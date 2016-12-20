#! /usr/bin/env bash

loops=10000
busywait=-b

peer=phobos2
# peer=three
# iface=ix0
# iface=enp4s0f0 # 40G
iface=enp8s0f0 # 10G
ip=10.11.12.3
piface=$iface
peerip=10.11.12.4

build=~/warpcore/$(uname -s)-rel

ssh="ssh $peer -q"

run () {
    echo "run $1"
    $ssh sudo bash << EOF
        pkill -f inetd
        cd $build/..
        nohup $build/examples/$1inetd -i $piface $busywait \
            > $build/../$1inetd.log 2>&1 &
EOF
    "$build/examples/$1ping" -i $iface -d "$peerip" -l $loops $busywait \
        >> "$1.txt" 2> "$1ping.log"
    $ssh "sudo pkill -f inetd"
}


pushd "$build"; ninja || exit; popd

sudo bash << EOF
    rm "$build"/../shim* "$build"/../warp* "$build"/../core > /dev/null 2>&1
    pkill -f "dhclient.*$iface"
    ifconfig $iface $ip/24 up
    if [ $(uname -s) == "Linux" ]; then
        ethtool -A $iface rx off tx off
        ethtool -C $iface rx-usecs 0
    else
        sysctl -q -w hw.ix.enable_aim=0
    fi
EOF

$ssh sudo bash << EOF
    pkill -f "dhclient.*$piface"
    ifconfig $piface $peerip/24 up
    if [ $(uname -s) == "Linux" ]; then
        ethtool -A $piface rx off tx off
        ethtool -C $piface rx-usecs 0
    else
        sysctl -q -w hw.ix.enable_aim=0
    fi
EOF

if [ ! "$1" ]; then
        run shim
fi

if [ "$(uname -s)" != "Linux" ]; then
    sudo ifconfig $iface -rxcsum -txcsum -tso -lro
fi

$ssh sudo bash << EOF
    if [ "$(uname -s)" != "Linux" ]; then
        ifconfig $piface -rxcsum -txcsum -tso -lro
    fi
EOF

if [ ! "$1" ]; then
        run warp
fi

if [ "$1" != "init" ]; then
    sudo bash <<EOF
        if [ $(uname -s) == "Linux" ]; then
            ip addr del $ip/24 dev $iface
        else
            ifconfig $iface -alias 10.11.12.3 rxcsum txcsum tso lro
        fi
EOF
    $ssh sudo bash << EOF
        if [ $(uname -s) == "Linux" ]; then
            ip addr del $peerip/24 dev $piface
        else
            ifconfig $piface -alias $peerip rxcsum txcsum tso lro
        fi
EOF
fi
