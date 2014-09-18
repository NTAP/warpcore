#! /usr/bin/env bash

set -e

iface=ix0
peer=mora2
peerip=172.16.5.32

cmd="FreeBSD/warpping -i $iface -d $peerip -l 20000"

if [ -z "$(ifconfig $iface | grep 'inet ')" ]; then
	echo local interface has no IP
	exit
fi

if [ -z "$(ssh $peer ifconfig $iface | grep 'inet ')" ]; then
        echo remote interface has no IP
        exit
fi

# # kill dhclient during the time the interfaces are in netmap mode
sudo pkill -f "dhclient.*$iface" || true
ssh $peer "sudo pkill -f 'dhclient.*$iface'" || true

run () {
	local flag
	if [ "$1" == "kern" ]; then
		flag=-k
	fi
#	for size in 16 100 200 300 400 500 600 700 800 900 1000 1100 1200 1300 1400 1458; do
	for (( size=16; size <= 1458; size+=103)); do
	# for (( size=16; size <= 1458; size+=503)); do
		echo Running $i size $size
		$cmd $flag -s $size > $1.$size.txt
	done
}


ssh $peer sudo pkill warpinetd || true
ssh $peer 'cd ~/warpcore; nohup FreeBSD/warpinetd -i ix0 > warpinetd.log 2>&1 &'
run warp
ssh $peer sudo pkill warpinetd || true

sleep 3
run kern

# restart dhclient
sudo dhclient $iface
ssh $peer sudo dhclient $iface
