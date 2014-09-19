#! /usr/bin/env bash

set -e

iface=em1
peeriface=eth1
peer=tux
peerip=192.168.125.129

cmd="FreeBSD/warpping -i $iface -d $peerip -l 10000"

if [ -z "$(/sbin/ifconfig $iface | grep 'inet ')" ]; then
	echo local interface has no IP
	exit
fi

if [ -z "$(ssh $peer /sbin/ifconfig $peeriface | grep 'inet ')" ]; then
        echo remote interface has no IP
        exit
fi

# # kill dhclient during the time the interfaces are in netmap mode
sudo pkill -INT -f "dhclient.*$iface" || true
ssh $peer "sudo pkill -INT -f 'dhclient.*$peeriface'" || true

run () {
	local flag
	if [ "$1" == "kern" ]; then
		flag=-k
	fi
	for (( size=16; size <= 1458; size+=103)); do
	# for (( size=16; size <= 1458; size+=303)); do
		echo "Running $1 size $size"
		$cmd $flag -s $size > "$1.$size.txt"
	done
}


ssh $peer sudo pkill -INT warpinetd || true
ssh $peer 'cd ~/warpcore; nohup FreeBSD/warpinetd -i ix0 > warpinetd.log 2>&1 &'
run warp
ssh $peer sudo pkill -INT warpinetd || true

# sleep 3
# run kern

# restart dhclient
sudo dhclient $iface
ssh $peer "sudo dhclient $peeriface"
