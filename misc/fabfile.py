from __future__ import with_statement
from fabric.api import *
from fabric.contrib.console import confirm
from fabric.contrib.files import *
from time import *

env.colorize_errors = True
env.use_ssh_config = True
env.builddir = ""
env.keeplog = False

env.ip = {"phobos1": "10.11.12.3",
          "phobos2": "10.11.12.4",
          "five": "10.11.12.5",
          "six": "10.11.12.6"}

env.tests = [
    # {"speed": 1, "client": "five", "server": "six", "iface": "ens1f1"},
    {"speed": 10, "client": "phobos1", "server": "phobos2", "iface": "enp8s0f0"},
    {"speed": 40, "client": "phobos1", "server": "phobos2", "iface": "enp4s0f0"},
]


@task
@parallel
@roles("client", "server")
def ip_config(iface):
    sudo("ifconfig %s %s/24 up" % (iface, env.ip[env.host_string]))
    with settings(warn_only=True):
        if run("uname -s") == "Linux":
            sudo("ethtool -C %s rx-usecs 0 tx-usecs 0 adaptive-rx off "
                 "adaptive-tx off rx-usecs-high 0" % iface)
            sudo("ethtool -L %s combined 2" % iface)


@task
@parallel
@roles("client", "server")
def ip_unconfig(iface):
    with settings(warn_only=True):
        if run("uname -s") == "Linux":
            for i in str.split(run("ls /sys/class/net")):
                sudo("ip addr del %s/24 dev %s" % (env.ip[env.host_string], i))
        else:
            for i in str.split(run("ifconfig -a")):
                sudo("ifconfig %s -alias %s" % i)


@parallel
@roles("client", "server")
def netmap_config(iface):
    with settings(warn_only=True):
        sudo("pkill -f 'dhclient.*%s'" % iface)
        if run("uname -s") == "Linux":
            sudo("echo 4096 > /sys/module/netmap/parameters/if_size")
            sudo("ethtool -A %s rx off tx off" % iface)
        else:
            sudo("sysctl -q -w hw.ix.enable_aim=0")
            sudo("sudo ifconfig %s -rxcsum -txcsum -tso -lro" % iface)


@parallel
@roles("client", "server")
def netmap_unconfig(iface):
    with settings(warn_only=True):
        if run("uname -s") == "Linux":
            # reload the driver module
            # driver = run("ethtool -i enp4s0f0 | head -n1 | cut -f2 -d:")
            # driver += "_netmap"
            # sudo("rmmod %s && modprobe %s" % (driver, driver))
            sudo("ethtool -A %s rx on tx on" % iface)
        else:
            sudo("sysctl -q -w hw.ix.enable_aim=1")
            sudo("sudo ifconfig %s rxcsum txcsum tso lro" % iface)


# @roles("client", "server")
@runs_once
def clear_logs():
    with cd("~/warpcore"):
        with settings(warn_only=True):
            sudo("rm warp*.log shim*.log warp*.txt shim*.txt 2> /dev/null")


@roles("client", "server")
@runs_once
def build():
    with cd("~/warpcore/"):
        dir = "%s-benchmarking" % run("uname -s")
        run("mkdir -p %s" % dir)
        with cd(dir):
            run("cmake -GNinja ..")  # -DCMAKE_BUILD_TYPE=Release
            run("ninja")
            env.builddir = run("pwd")


@parallel
@roles("server")
def start_server(test, busywait, cksum, kind):
    with cd(env.builddir):
        log = "../%sinetd-%s%s%s.log" % (kind, test["speed"], busywait, cksum)
        if not env.keeplog:
            log = "/dev/null"
        sudo("nohup examples/%sinetd -i %s %s %s > %s 2>&1 &" %
             (kind, test["iface"], busywait, cksum, log))


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


@roles("client")
def start_client(test, busywait, cksum, kind):
    with cd(env.builddir):
        prefix = "../%sping-%s%s%s" % (kind, test["speed"], busywait, cksum)
        file = prefix + ".txt"
        log = prefix + ".log"
        if not env.keeplog:
            log = "/dev/null"
        sudo("examples/%sping -i %s -d %s %s %s -l 100000 > %s 2> %s" %
             (kind, test["iface"], env.ip[test["server"]],
              busywait, cksum, file, log))


@task(default=True)
def bench():
    with settings(hosts=env.tests[0]["client"]):
        execute(clear_logs)

    for t in env.tests:
        print(t["speed"])
        with settings(roledefs={"client": [t["client"]],
                                "server": [t["server"]]}):
            execute(build)

            execute(stop)
            execute(netmap_unconfig, t["iface"])
            execute(ip_unconfig, t["iface"])

            execute(ip_config, t["iface"])

            for k in ["warp", "shim"]:
                if k == "warp":
                    execute(netmap_config, t["iface"])
                for c in ["-z", ""]:
                    for w in ["-b", ""]:
                        sleep(3)
                        execute(start_server, t, w, c, k)
                        sleep(3)
                        execute(start_client, t, w, c, k)
                        execute(stop)
                if k == "warp":
                    execute(netmap_unconfig, t["iface"])

            execute(ip_unconfig, t["iface"])
