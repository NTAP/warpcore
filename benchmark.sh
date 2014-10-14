#! /usr/bin/env bash

set -e

loops=100000
busywait=-b
aim=0

peer=mora2
iface=ix0
piface=$iface


iname=$(echo $iface | tr -d 0-9)
peerip=$(ssh $peer "/sbin/ifconfig $piface" | sed -e s/addr:// -e s/^[[:space:]]*//g | grep 'inet ' | cut -f 2 -d' ')

os=$(uname -s)
peeros=$(ssh $peer "uname -s")

run () {
	local flag
	if [ "$1" == "kern" ]; then
		flag=-k
	fi
	local cmd="$os/warpping -i $iface -d $peerip -l $loops $busywait"
	for (( size=16; size <= 1458; size+=103)); do
	# for (( size=16; size <= 1458; size+=303)); do
		echo "Running $1 size $size"
		$cmd $flag -s $size >> "$1.txt" 2> "warpping.$1.log"
	done
	echo "nsec	size" > "$1.new"
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

# kill dhclient during the time the interfaces are in netmap mode
sudo pkill -f "dhclient.*$iface" || true
sudo sysctl "hw.$iname.enable_aim=$aim" || true
sudo cpuset -l 1 -p $(pgrep ^inetd)

ssh $peer "sudo pkill -f 'dhclient.*$piface'" || true
ssh $peer "pkill -INT -f warpinetd" || true
ssh $peer "sudo sysctl hw.$iname.enable_aim=$aim" || true
ssh $peer 'sudo cpuset -l 1 -p $(pgrep ^inetd)' || true

ssh $peer "cd ~/warpcore; nohup $peeros/warpinetd -i $piface $busywait > warpinetd.log 2>&1 &" || true
run warp
ssh $peer "pkill -INT -f warpinetd" || true

ssh $peer "sudo dhclient $piface" || true
sleep 5
run kern

sudo dhclient $iface
