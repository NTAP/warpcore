#! /usr/bin/env bash

set -e

iface=ix0
piface=ix0
# iface=igb0
# piface=igb0
iname=$(echo $iface | tr -d 0-9)
peer=six
peerip=$(ssh $peer "/sbin/ifconfig $piface | grep 'inet ' | cut -f 2 -d' '")
loops=100000

busywait=-b

run () {
	local flag
	if [ "$1" == "kern" ]; then
		flag=-k
	fi
	local cmd="FreeBSD/warpping -i $iface -d $peerip -l $loops $busywait"
	for (( size=16; size <= 1458; size+=103)); do
	# for (( size=16; size <= 1458; size+=303)); do
		echo "Running $1 size $size"
		$cmd $flag -s $size >> "$1.txt"
	done
	echo "us	codeus	size" > $1.new
	grep -v codeus $1.txt >> $1.new
	mv $1.new $1.txt
}


if [ -z "$(/sbin/ifconfig $iface | grep 'inet ')" ]; then
	echo local interface has no IP address
	exit
fi

if [ -z "$peerip" ]; then
        echo remote interface has no IP address
        exit
fi

rm kern*.txt warp*.txt > /dev/null 2>&1 || true

# kill dhclient during the time the interfaces are in netmap mode
sudo pkill -f "dhclient.*$iface" || true
sudo sysctl hw.$iname.enable_aim=0 || true
sudo cpuset -l 1 -p $(pgrep ^inetd)

ssh $peer "sudo pkill -f 'dhclient.*$piface'" || true
ssh $peer "pkill -INT -f warpinetd; pkill -INT -f warpinetd" || true
ssh $peer "sudo sysctl hw.$iname.enable_aim=0" || true
ssh $peer 'sudo cpuset -l 1 -p $(pgrep ^inetd)' || true

ssh $peer "cd ~/warpcore; nohup FreeBSD/warpinetd -i $piface $busywait > warpinetd.log 2>&1 &" || true
run warp
ssh $peer "pkill -INT -f warpinetd; pkill -INT -f warpinetd" || true

ssh $peer "sudo dhclient $piface" || true
sleep 3
run kern

sudo dhclient $iface
