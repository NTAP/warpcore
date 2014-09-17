#! /bin/sh

iface=ix0
cmd="FreeBSD/warpping -i $iface -d 172.16.20.1 -l 10000"


if [ -z "$(ifconfig $iface | grep 'inet ')" ]; then
	echo local interface has no IP
	exit
fi

if [ -z "$(ssh mora2 ifconfig $iface | grep 'inet ')" ]; then
        echo remote interface has no IP
        exit
fi


run () {
	local flag
	if [ "$1" == "kern" ]; then
		flag=-k
	fi
	for size in 16 32 64 128 256 512 1024 1450; do
		$cmd $flag -s $size > $1.$size.txt
	done
}


run kern

pid=$(ssh mora2 'cd ~/warpcore; nohup FreeBSD/warpinetd -i ix0 > warpinetd.log 2>&1 & echo $!')
run warp
ssh mora2 kill $pid
