from __future__ import with_statement
from fabric.api import *
from fabric.contrib.console import confirm
from fabric.contrib.files import *
from time import *

env.colorize_errors = True
env.use_ssh_config = True
env.builddir = ""
env.keeplog = True
env.uname = {}

env.ip = {"phobos1": "10.11.12.3",
          "phobos2": "10.11.12.4",
          "five": "10.11.12.5",
          "six": "10.11.12.6",
          "mora1": "10.11.12.5",
          "mora2": "10.11.12.6",
          }

env.tests = [
    # {"speed": 1, "client": "five", "server": "six", "iface": "enp9s0f0"},
    {"speed": 1, "client": "phobos1", "server": "phobos2", "iface": "eno2"},
    {"speed": 10, "client": "phobos1", "server": "phobos2", "iface": "enp8s0f0"},
    {"speed": 40, "client": "phobos1", "server": "phobos2", "iface": "enp4s0f0"},
    # {"speed": 1, "client": "mora1", "server": "mora2", "iface": "igb1"},
    # {"speed": 10, "client": "mora1", "server": "mora2", "iface": "ix0"},
    # {"speed": 40, "client": "mora1", "server": "mora2", "iface": "ixl0"},
]


@task
@parallel
@roles("client", "server")
def ip_config(iface):
    sudo("ifconfig %s %s/24 up" % (iface, env.ip[env.host_string]))
    with settings(warn_only=True):
        if env.uname[env.host_string] == "Linux":
            sudo("ethtool -C %s adaptive-rx off adaptive-tx off "
                 "rx-usecs 0 tx-usecs 0; "
                 "ethtool -L %s combined 1" % (iface, iface))


@task
@parallel
@roles("client", "server")
def ip_unconfig(iface):
    with settings(warn_only=True):
        cmd = ""
        if env.uname[env.host_string] == "Linux":
            for i in run("ls /sys/class/net").split():
                cmd += "ip addr del %s/24 dev %s; " % (env.ip[env.host_string], i)
        else:
            for i in run("ifconfig -l").split():
                cmd += "ifconfig %s -alias %s; " % (i, env.ip[env.host_string])
        sudo(cmd)


@parallel
@roles("client", "server")
def netmap_config(iface):
    with settings(warn_only=True):
        if env.uname[env.host_string] == "Linux":
            sudo("echo 4096 > /sys/module/netmap/parameters/if_size; "
                 "ethtool -K %s sg off rx off tx off tso off gro off lro off" %
                 iface)
        else:
            sudo("sysctl -q -w hw.ix.enable_aim=0; "
                 "sysctl -q -w dev.netmap.admode=1;     "
                 "ifconfig %s -rxcsum -txcsum -tso -lro" % iface)


@parallel
@roles("client", "server")
def netmap_unconfig(iface):
    with settings(warn_only=True):
        if env.uname[env.host_string] == "Linux":
            # reload the driver module
            # driver = run("ethtool -i enp4s0f0 | head -n1 | cut -f2 -d:")
            # driver += "_netmap"
            # sudo("rmmod %s && modprobe %s" % (driver, driver))
            sudo("ethtool -K %s sg on rx on tx on tso on gro on lro on" %
                 iface)
        else:
            sudo("sysctl -q -w hw.ix.enable_aim=1; "
                 "ifconfig %s rxcsum txcsum tso lro" % iface)


@runs_once
def clear_logs():
    with cd("~/warpcore"):
        with settings(warn_only=True):
            sudo("rm warp*.log shim*.log warp*.txt shim*.txt 2> /dev/null")


@roles("client", "server")
@runs_once
def build():
    with cd("~/warpcore/"):
        dir = "%s-benchmarking" % env.uname[env.host_string]
        run("mkdir -p %s" % dir)
        with cd(dir):
            run("cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -GNinja ..; ninja")
            env.builddir = run("pwd")


@parallel
@roles("server")
def start_server(test, busywait, cksum, kind):
    with cd(env.builddir):
        log = "../%sinetd-%s%s%s.log" % (kind, test["speed"], busywait, cksum)
        if not env.keeplog:
            log = "/dev/null"
        sudo("/usr/bin/nice -20 /usr/bin/nohup "
             "bin/%sinetd -i %s %s %s 2>&1 > %s &" %
             (kind, test["iface"], busywait, cksum, log))


@task
@parallel
@roles("client", "server")
def stop(flag=""):
    with settings(warn_only=True):
        sudo("pkill %s '(warp|shim)(ping|inetd)'; pkill %s inetd" % (flag, flag))
    run('''while : ; do \
            pgrep "(warp|shim)(ping|inetd)"; \
            [ "$?" == 1 ] && break; \
            sleep 1; \
        done''')


@task
@roles("client", "server")
def uname():
    env.uname[env.host_string] = run("uname -s")


@roles("client")
def start_client(test, busywait, cksum, kind):
    with cd(env.builddir):
        prefix = "../%sping-%s%s%s" % (kind, test["speed"], busywait, cksum)
        file = prefix + ".txt"
        log = prefix + ".log"
        if not env.keeplog:
            log = "/dev/null"
        sudo("nice -20 bin/%sping -i %s -d %s %s %s -l 1000 > %s 2> %s" %
             (kind, test["iface"], env.ip[test["server"]],
              busywait, cksum, file, log))


@task(default=True)
def bench():
    with settings(hosts=env.tests[0]["client"]):
        execute(clear_logs)

    for t in env.tests:
        with settings(roledefs={"client": [t["client"]],
                      "server": [t["server"]]}):
            execute(uname)
            execute(build)
            execute(stop)
            execute(netmap_unconfig, t["iface"])
            execute(ip_unconfig, t["iface"])
            execute(ip_config, t["iface"])

            for k in ["warp", "shim"]:
                if k == "warp":
                    execute(netmap_config, t["iface"])
                # for c in ["-z", ""]:
                c = ""
                for w in ["-b", ""]:
                    sleep(3)
                    execute(start_server, t, w, c, k)
                    sleep(3)
                    execute(start_client, t, w, c, k)
                    execute(stop)
                if k == "warp":
                    execute(netmap_unconfig, t["iface"])
            execute(ip_unconfig, t["iface"])
