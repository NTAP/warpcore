from __future__ import with_statement
from fabric.api import *
from fabric.contrib.console import confirm
from fabric.contrib.files import *
from time import *

env.colorize_errors = True
env.use_ssh_config = True
env.builddir = {}
env.keeplog = False

env.roledefs = {"client": ["phobos1"],
                "server": ["phobos2"], }

env.ifaces = [{"speed": 10, "name": "enp8s0f0"},
              {"speed": 40, "name": "enp4s0f0"}, ]

env.ip = {"phobos1": "10.11.12.3",
          "phobos2": "10.11.12.4", }


@task
@parallel
@roles("client", "server")
def ip_config(iface):
    sudo("ifconfig %s %s/24 up" % (iface, env.ip[env.host_string]))
    # run("sleep 3")


@task
@parallel
@roles("client", "server")
def ip_unconfig(iface):
    with settings(warn_only=True):
        if run("uname -s") == "Linux":
            sudo("ip addr del %s/24 dev %s" %
                 (env.ip[env.host_string], iface))
        else:
            sudo("ifconfig %s -alias 10.11.12.3 rxcsum txcsum tso lro" %
                 iface)


@parallel
@roles("client", "server")
def netmap_config(iface):
    with settings(warn_only=True):
        sudo("pkill -f 'dhclient.*%s'" % iface)
        if run("uname -s") == "Linux":
            sudo("echo 4096 > /sys/module/netmap/parameters/if_size")
            sudo("ethtool -A %s rx off tx off" % iface)
            sudo("ethtool -C %s rx-usecs 0 tx-usecs 0 adaptive-rx off "
                 "adaptive-tx off rx-usecs-high 0" % iface)
            sudo("ethtool -L %s combined 2" % iface)
        else:
            sudo("sysctl -q -w hw.ix.enable_aim=0")
            sudo("sudo ifconfig %s -rxcsum -txcsum -tso -lro" % iface)
    # run("sleep 3")


@parallel
@roles("client", "server")
def netmap_unconfig(iface):
    with settings(warn_only=True):
        if run("uname -s") == "Linux":
            sudo("ethtool -A %s rx on tx on" % iface)
            # XXX need to figure out what the defaults are
            # sudo("ethtool -C %s rx-usecs 1 tx-usecs 0 adaptive-rx on "
            #      "adaptive-tx on rx-usecs-high 0" % iface)
            sudo("ethtool -L %s combined 63" % iface)
        else:
            sudo("sysctl -q -w hw.ix.enable_aim=1")
            sudo("sudo ifconfig %s rxcsum txcsum tso lro" % iface)


@parallel
@roles("client", "server")
def clear_logs():
    with cd("~/warpcore"):
        with settings(warn_only=True):
            sudo("rm warp*.log shim*.log warp*.txt shim*.txt")


@roles("client", "server")
def build():
    with cd("~/warpcore/"):
        dir = "%s-benchmarking" % run("uname -s")
        run("mkdir -p %s" % dir)
        with cd(dir):
            run("cmake -GNinja ..")  # -DCMAKE_BUILD_TYPE=Release
            run("ninja")
            env.builddir[env.host_string] = run("pwd")


@parallel
@roles("server")
def start_server(iface, busywait, cksum, kind):
    with cd(env.builddir[env.host_string]):
        file = "../%sinetd-%s%s%s" % (kind, iface["speed"], busywait, cksum)
        sudo("nohup examples/%sinetd -i %s %s %s > %s.log 2>&1 &" %
             (kind, iface["name"], busywait, cksum, file))


@task
@parallel
@roles("client", "server")
def stop(flag=""):
    with settings(warn_only=True):
        sudo("pkill %s '(warp|shim)(ping|inetd)'" % flag)
    run('''while : ; do \
            pgrep "(warp|shim)(ping|inetd)"; \
            [ "$?" == 1 ] && break; \
            sleep 1; \
        done''')
    # run("ps -ef | ag 'warp|shim'")


@roles("client")
def start_client(iface, peerip, busywait, cksum, kind):
    with cd(env.builddir[env.host_string]):
        file = "../%sping-%s%s%s" % (kind, iface["speed"], busywait, cksum)
        sudo("examples/%sping -i %s -d %s %s %s -l 100000 "
             "> %s.txt 2> %s.log" %
             (kind, iface["name"], peerip, busywait, cksum, file, file))


@task(default=True)
def bench():
    execute(build)

    # clean up after possibly failed previous run
    execute(clear_logs)
    execute(stop)
    for i in env.ifaces:
        execute(ip_unconfig, i["name"])

    # start run
    for i in env.ifaces:
        execute(ip_config, i["name"])
        for k in ["warp", "shim"]:
            if k == "warp":
                execute(netmap_config, i["name"])
            for c in ["-z", ""]:
                for w in ["-b", ""]:
                    sleep(3)
                    execute(start_server, i, w, c, k)
                    sleep(3)
                    execute(start_client, i, env.ip[env.roledefs["server"][0]],
                            w, c, k)
                    execute(stop)
            # if k == "warp":
                # execute(netmap_unconfig, i["name"])
        execute(ip_unconfig, i["name"])
