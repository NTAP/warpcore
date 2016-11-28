#! /usr/bin/env bash

loops=10000
busywait=-b

peer=four
iface=ix0
piface=ix0
build=~/warpcore/freebsd-rel

ssh="ssh $peer -q"

run () {
    echo run $1
    $build/examples/$1ping -i $iface -d $peerip -l $loops $busywait \
        >> "$1.txt" 2> "$1ping.log"
}

rm warp*.txt shim*.txt ./*.log ./*.core ./*.gmon > /dev/null 2>&1

sudo pkill -f "dhclient: $iface"
sudo ifconfig $iface 10.11.12.3/24

$ssh "sudo pkill -f \"dhclient: $piface\""
$ssh "sudo ifconfig $piface 10.11.12.4/24"
$ssh "pkill -f warpinetd"
peerip=$(
$ssh "/sbin/ifconfig $piface" | \
    sed -e s/addr:// -e s/^[[:space:]]*//g | \
    grep 'inet ' | cut -f 2 -d' '
)


if [ -z "$(/sbin/ifconfig $iface | grep 'inet ')" ]; then
    echo local interface has no IP address
    exit
fi

if [ -z "$peerip" ]; then
    echo remote interface has no IP address
    exit
fi


run shim

$ssh "(cd $build/.. && \
        nohup $build/examples/warpinetd -i $piface $busywait ) \
        > warpinetd.log 2>&1 &"
run warp

$ssh "pkill -f warpinetd"
sudo ifconfig $iface -alias 10.11.12.3
$ssh "sudo ifconfig $piface -alias 10.11.12.4"
