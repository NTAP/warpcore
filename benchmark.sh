#! /usr/bin/env bash

set -e

iface=ix0
peeriface=ix0
# iface=igb0
# peeriface=igb0
peer=mora2
peerip=$(ssh $peer "ifconfig $peeriface | grep 'inet ' | cut -f 2 -d' '")

run () {
	local flag
	if [ "$1" == "kern" ]; then
		flag=-k
	fi
	local cmd="FreeBSD/warpping -i $iface -d $peerip -l 10000"
	# for (( size=16; size <= 1458; size+=103)); do
	for (( size=16; size <= 1458; size+=303)); do
		echo "Running $1 size $size"
		$cmd $flag -s $size > "$1.$size.txt"
	done
}


if [ -z "$(/sbin/ifconfig $iface | grep 'inet ')" ]; then
	echo local interface has no IP
	exit
fi

if [ -z "$peerip" ]; then
        echo remote interface has no IP
        exit
fi

rm kern*.txt warp*.txt > /dev/null 2>&1 || true

# kill dhclient during the time the interfaces are in netmap mode
sudo pkill -f "dhclient.*$iface" || true
ssh $peer "sudo pkill -f 'dhclient.*$peeriface'" || true

ssh $peer "sudo pkill -INT warpinetd; cd ~/warpcore; nohup FreeBSD/warpinetd -i $peeriface > warpinetd.log 2>&1 &"
run warp
ssh $peer "sudo pkill -INT warpinetd" || true
sleep 3
run kern

# restart dhclient
sudo dhclient $iface
ssh $peer "sudo dhclient $peeriface"
