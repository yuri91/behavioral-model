#!/usr/bin/env python3

import subprocess
import shlex
import os
import time
import signal
import argparse

parser = argparse.ArgumentParser(description='test target performance')
parser.add_argument('--malloc', help='specify malloc implementation',
                    type=str, action="store", default="malloc")
parser.add_argument('--target', help='specify target of test',
                    type=str, action="store", default="simple_router")
parser.add_argument('--program', help='specify p4 program of test',
                    type=str, action="store", default="simple_router")
parser.add_argument('--use-netmap', help='use netmap instead of BMI',
                    dest="use_netmap", action="store_true", default=False)
parser.add_argument('--use-netmap-pipes', help='use netmap pipes instead of veth',
                    dest="use_pipes", action="store_true", default=False)

def sh(cmd, **kwargs):
    args = shlex.split(cmd)
    kwargs["stdin"] = kwargs.get("stdin",subprocess.PIPE)
    kwargs["stdout"] = kwargs.get("stdout",subprocess.PIPE)
    kwargs["stderr"] = kwargs.get("stderr",subprocess.PIPE)
    return subprocess.Popen(args, **kwargs)

def veth_add(intf1, intf2):
    sh("ip link add {} type veth peer name {}".format(intf1,intf2)).wait()

def veth_up(intf):
    sh("ip link set {} up".format(intf)).wait()

def veth_del(intf):
    sh("ip link del {}".format(intf)).wait()

def veth_setup(intf1c, intf1s, intf2c, intf2s):
    veth_add(intf1c, intf1s)
    veth_add(intf2c, intf2s)

    veth_up(intf1c)
    veth_up(intf1s)
    veth_up(intf2c)
    veth_up(intf2s)

def veth_teardown(intf1, intf2):
    veth_del(intf1)
    veth_del(intf2)

if __name__ == "__main__":
    args = parser.parse_args()
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    netmap_pipes = args.use_pipes

    extra_opts = ""
    if args.use_netmap:
        extra_opts = "--use-netmap"
    if not netmap_pipes:
        intf1c = "veth1"
        intf1s = "veth1s"
        intf2c = "veth2"
        intf2s = "veth2s"
        veth_setup(intf1c,intf1s,intf2c,intf2s)
    else:
        intf1c = "vale1{1"
        intf1s = "vale1}1"
        intf2c = "vale2{2"
        intf2s = "vale2}2"

    env = {}
    if args.malloc != "malloc":
        env={'LD_PRELOAD' : '/usr/lib/lib{}.so'.format(args.malloc)}

    switch = sh("../targets/{0}/{0} {4}  -i 1@{1} -i 2@{2} {3}.json"
                .format(args.target, intf1s, intf2s, args.program, extra_opts),
                env=env,
                stdout=None, stderr=None
                )
    time.sleep(1)

    runtime = sh("../tools/runtime_CLI.py", stdin=open("{0}_commands.txt".format(args.program),"r"))
    runtime.wait()

    h1 = sh("pkt-gen -i {} -f tx -d 10.0.1.10 -D 00:04:00:00:00:01".format(intf1c))
    h2 = sh("pkt-gen -i {} -f rx".format(intf2c))


    time.sleep(10)
    h2.send_signal(signal.SIGINT)
    time.sleep(.1)
    h1.terminate()
    if switch.poll() != None:
        print("switch CRASHED! "+str(switch.returncode))
    else:
        switch.terminate()
    s = h1.stderr.readlines()[-1].decode('utf-8').strip()
    h2.wait()
    switch.wait()
    h1.wait()

    if not netmap_pipes:
        veth_teardown(intf1c, intf2c)

    print(s)
