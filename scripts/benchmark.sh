#! /usr/bin/env bash

set -e

key=/vagrant/scripts/vagrant
loops=1000
# busywait=-b
# aim=0

peer=10.0.2.15
iface=enp0s8
piface=enp0s8
peerip=$(ssh -i $key ubuntu@$peer "/sbin/ifconfig $piface" | sed -e s/addr:// -e s/^[[:space:]]*//g | grep 'inet ' | cut -f 2 -d' ')

run () {
        local flag
        if [ "$1" == "kern" ]; then
                flag=-k
        fi
        local cmd="/vagrant/examples/warpping -i $iface -d $peerip -l $loops $busywait"
        # for (( size=16; size <= 1458; size+=103)); do
        for (( size=16; size <= 1458; size+=303)); do
                echo "Running $1 size $size"
                $cmd $flag -s $size >> "$1.txt" 2> "warpping.$1.log"
        done
        echo "nsec      size" > "$1.new"
        grep -v nsec "$1.txt" >> "$1.new"
        mv "$1.new" "$1.txt"
}


if [ -z "$(/sbin/ifconfig $iface | grep 'inet ')" ]; then
        echo local interface has no IP address
        exit
fi

if [ -z "$peerip" ]; then
        echo remote interface has no IP address
        exit
fi

rm kern*.txt warp*.txt ./*.log > /dev/null 2>&1 || true

ssh -i $key ubuntu@$peer "pkill -f warpinetd; ps -ef | grep warp" || true

ssh -i $key ubuntu@$peer "cd /vagrant; /vagrant/examples/warpinetd -i $piface $busywait > warpinetd.log &" || true
run warp
ssh -i $key ubuntu@$peer "pkill -f warpinetd; ps -ef | grep warp" || true

sleep 5
run kern
