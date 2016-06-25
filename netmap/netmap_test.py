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

def sh(cmd, **kwargs):
    args = shlex.split(cmd)
    kwargs["stdin"] = kwargs.get("stdin",subprocess.PIPE)
    kwargs["stdout"] = kwargs.get("stdout",subprocess.PIPE)
    kwargs["stderr"] = kwargs.get("stderr",subprocess.PIPE)
    return subprocess.Popen(args, **kwargs)


if __name__ == "__main__":
    args = parser.parse_args()
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    env = {}
    if args.malloc != "malloc":
        env={'LD_PRELOAD' : '/usr/lib/lib{}.so'.format(args.malloc)}

    switch = sh("../targets/{0}/{0} --use-netmap  -i 1@vale1{{1 -i 2@vale2{{2 {1}.json"
                .format(args.target,args.program),
                env=env,
                stdout=None
                )
    time.sleep(1)

    runtime = sh("../tools/runtime_CLI.py", stdin=open("{0}_commands.txt".format(args.program),"r"))
    runtime.wait()

    h1 = sh("pkt-gen -i vale1}1 -f tx -d 10.0.1.10 -D 00:04:00:00:00:01")
    h2 = sh("pkt-gen -i vale2}2 -f rx")


    time.sleep(10)
    h2.send_signal(signal.SIGINT)
    time.sleep(.1)
    h1.terminate()
    switch.terminate()
    s = h2.stdout.readlines()[-1].decode('utf-8').strip()
    h2.wait()
    switch.wait()
    h1.wait()
    print(s)
