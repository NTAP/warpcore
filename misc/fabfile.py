from __future__ import with_statement
from fabric.api import *
from fabric.contrib.files import *
from time import *

env.colorize_errors = True
env.use_ssh_config = True
env.builddir = ""
env.uname = {}

env.ip = {"phobos1": "10.11.12.3",
          "phobos2": "10.11.12.4",
          "five": "10.11.12.5",
          "six": "10.11.12.6",
          "mora1": "10.11.12.7",
          "mora2": "10.11.12.8",
          }

env.tests = [
    # phobos Linux
    # {"speed": 1, "iter": 10, "client": "phobos1", "server": "phobos2",
    #  "client_iface": "eno2", "server_iface": "eno2"},
    # {"speed": 10, "iter": 50, "client": "phobos1", "server": "phobos2",
    #  "client_iface": "enp7s0f1", "server_iface": "enp7s0f1"},
    # {"speed": 40, "iter": 100, "client": "phobos1", "server": "phobos2",
    #  "client_iface": "enp4s0f0", "server_iface": "enp4s0f0"},

    # mora FreeBSD
    # {"speed": 1, "client": "mora1", "server": "mora2", "iface": "igb5"},
    # {"speed": 10, "client": "mora1", "server": "mora2", "iface": "ix0"},
    # {"speed": 40, "client": "mora1", "server": "mora2", "iface": "ixl0"},

    # mora Linux
    {"speed": 1, "iter": 10, "client": "mora1", "server": "mora2",
     "client_iface": "eno3", "server_iface": "eno3"},
    {"speed": 10, "iter": 50, "client": "mora1", "server": "mora2",
     "client_iface": "enp2s0f0", "server_iface": "enp2s0f0"},
    {"speed": 40, "iter": 100, "client": "mora1", "server": "mora2",
     "client_iface": "enp6s0f0", "server_iface": "enp6s0f0"},
]


def get_iface(test):
    if env.host_string in env.roledefs["client"]:
        return test["client_iface"]
    else:
        return test["server_iface"]


@task
@parallel
@roles("client", "server")
def ip_config(test):
    iface = get_iface(test)
    with settings(warn_only=True):
        cmd = ""
        if env.uname[env.host_string] == "Linux":
            cmd += ("sysctl net.core.rmem_max=26214400"
                    "       net.core.wmem_max=26214400"
                    "       net.core.rmem_default=26214400"
                    "       net.core.wmem_default=26214400; "
                    "ifconfig %s down; "
                    "ethtool -C %s adaptive-rx off adaptive-tx off "
                    "rx-usecs 0 tx-usecs 0; "
                    "ethtool -C %s rx-frames-irq 1 tx-frames-irq 1; "
                    "ethtool -G %s rx 512 tx 512; "
                    "ethtool -A %s rx off tx off; "
                    "ethtool -L %s combined 2; "
                    "ethtool --set-eee %s eee off; "
                    "ifconfig %s up; " % ((iface, ) * 8))
        else:
            cmd += ("pkill -f 'dhclient: %s'; " % iface)
        cmd += ("ifconfig %s %s/24 up; " % (iface, env.ip[env.host_string]))
        sudo(cmd)


@task
@parallel
@roles("client", "server")
def ip_unconfig(test):
    iface = get_iface(test)
    with settings(warn_only=True):
        cmd = ""
        if env.uname[env.host_string] == "Linux":
            for i in run("ls /sys/class/net").split():
                cmd += ("ip addr del %s/24 dev %s; " %
                        (env.ip[env.host_string], i))
                cmd += ("ethtool -G %s rx 512 tx 512; " % (iface))
        else:
            for i in run("ifconfig -l").split():
                cmd += "ifconfig %s -alias %s; " % (i, env.ip[env.host_string])
        sudo(cmd)


@parallel
@roles("client", "server")
def netmap_config(test):
    iface = get_iface(test)
    with settings(warn_only=True):
        if env.uname[env.host_string] == "Linux":
            sudo("echo 4096 > /sys/module/netmap/parameters/if_size; "
                 "echo 1 > /sys/module/netmap/parameters/admode; "
                 "echo 1000000 > /sys/module/netmap/parameters/buf_num; "
                 "ifconfig %s down; "
                 "ethtool -K %s sg off rx off tx off tso off gro off lro off; "
                 "ifconfig %s up; " % ((iface, ) * 3))
        else:
            sudo("sysctl hw.ix.enable_aim=0; "
                 "sysctl dev.netmap.if_size=4096; "
                 "sysctl dev.netmap.admode=1; "
                 "sysctl dev.netmap.buf_num=1000000; "
                 "ifconfig %s -rxcsum -txcsum -tso -lro; " % (iface))


@parallel
@roles("client", "server")
def netmap_unconfig(test):
    iface = get_iface(test)
    with settings(warn_only=True):
        if env.uname[env.host_string] == "Linux":
            sudo("ifconfig %s down; "
                 "ethtool -K %s sg on rx on tx on tso on gro on lro on; "
                 "ethtool -C %s adaptive-rx on adaptive-tx on "
                 "rx-usecs 10 ; "
                 "ifconfig %s up; " % ((iface, ) * 4))
        else:
            sudo("sysctl hw.ix.enable_aim=1; "
                 "ifconfig %s rxcsum txcsum tso lro; " % (iface))


@runs_once
def clear_logs():
    with cd("~/warpcore"):
        with settings(warn_only=True):
            sudo("rm warp*.log sock*.log warp*.txt sock*.txt "
                 "warp*.prof sock*.prof > /dev/null")


@roles("client", "server")
@runs_once
def build():
    with cd("~/warpcore/"):
        dir = "%s-benchmarking" % env.uname[env.host_string]
        run("mkdir -p %s" % dir)
        with cd(dir):
            run("git pull --recurse-submodules; "
                "cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBENCHMARKING=1 "
                "-GNinja ..; ninja")
            with settings(warn_only=True):
                sudo("rm *core")
            env.builddir = run("pwd")


@parallel
@roles("server")
def start_server(test, busywait, cksum, kind):
    pin = ""
    if env.uname[env.host_string] == "Linux":
        pin = "/usr/bin/taskset -c"
        preload = "/usr/lib/x86_64-linux-gnu/libprofiler.so"
    else:
        pin = "/usr/bin/cpuset -l"
        preload = ""
    with cd(env.builddir):
        prefix = "../%sinetd-%s%s%s" % (kind, test["speed"], busywait, cksum)
        log = prefix + ".log"
        prof = prefix + ".prof"
        sudo("(/usr/bin/nohup %s 3 env LD_PRELOAD=%s "
             "CPUPROFILE=%s CPUPROFILE_FREQUENCY=10000 "
             "%s/bin/%sinetd -i %s %s %s) 2>&1 > %s &" %
             (pin, preload, prof, env.builddir, kind, test["server_iface"],
              busywait, cksum, log))


@task
@parallel
@roles("client", "server")
def stop(flag=""):
    with settings(warn_only=True):
        with cd("~/warpcore"):
            sudo('''pkill %s inetd ; \
                    while : ; do \
                        pkill %s '(warp|sock)(ping|inetd)'; \
                        pgrep "(warp|sock)(ping|inetd)"; \
                        [ "$?" == 1 ] && break; \
                        sleep 3; \
                    done; \
                    for log in *.log; do \
                        sed -E -i'' '/^(PROFILE: interrupts.*)?$/d' "$log"; \
                        [ ! -s "$log" ] && rm "$log"; \
                    done; \
                ''' % (flag, flag))


@task
@roles("client", "server")
def uname():
    env.uname[env.host_string] = run("uname -s")


@roles("client")
def start_client(test, busywait, cksum, kind):
    pin = ""
    if env.uname[env.host_string] == "Linux":
        pin = "/usr/bin/taskset -c"
        preload = "/usr/lib/x86_64-linux-gnu/libprofiler.so"
    else:
        pin = "/usr/bin/cpuset -l"
        preload = ""
    with cd(env.builddir):
        prefix = "../%sping-%s%s%s" % (kind, test["speed"], busywait, cksum)
        file = prefix + ".txt"
        log = prefix + ".log"
        prof = prefix + ".prof"
        sudo("(%s 3 env LD_PRELOAD=%s "
             "CPUPROFILE=%s CPUPROFILE_FREQUENCY=10000 "
             "%s/bin/%sping -i %s -d %s %s %s -l %s "
             "-s 32 -p 0 -e 17000000) > %s 2> %s" %
             (pin, preload, prof, env.builddir, kind, test["client_iface"],
              env.ip[test["server"]], busywait, cksum, test["iter"],
              file, log))


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
            execute(netmap_unconfig, t)
            execute(ip_unconfig, t)
            execute(ip_config, t)

            # XXX need to test sock before warp, otherwise 40G sock perf sucks?
            for k in ["warp", "sock"]:
                if k == "warp":
                    execute(netmap_config, t)
                for c in ["-z", ""]:
                    for w in ["-b", ""]:
                        execute(start_server, t, w, c, k)
                        sleep(3)  # switch needs some time to settle
                        execute(start_client, t, w, c, k)
                        execute(stop)
                if k == "warp":
                    execute(netmap_unconfig, t)
            execute(ip_unconfig, t)
