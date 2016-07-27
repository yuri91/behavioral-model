#!/usr/bin/python

# Copyright 2013-present Barefoot Networks, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

from mininet.net import Mininet
from mininet.topo import Topo
from mininet.log import setLogLevel, info

from p4_mininet import P4Switch, P4Host

import argparse
from time import sleep
import subprocess
import os
import re
import time
import signal
import math

parser = argparse.ArgumentParser(description='Mininet demo')
parser.add_argument('--thrift-port',
                    help='Thrift server port for table updates',
                    type=int, action="store", default=9090)
parser.add_argument('--repeat', '-r',
                    help='Number of times to run test',
                    type=int, action="store", default=5)
parser.add_argument('--target', '-t',
                    help='Target switch to use',
                    type=str, action="store", default='simple_switch')
parser.add_argument('--use-netmap', '-n',
                    help='Use netmap for packet I/O',
                    action="store_true")
args = parser.parse_args()

class SingleSwitchTopo(Topo):
    "Single switch connected to 2 hosts."
    def __init__(self, sw_path, json_path, thrift_port, use_netmap=False, **opts):
        # Initialize topology and default options
        Topo.__init__(self, **opts)

        switch = self.addSwitch('s1',
                                sw_path = sw_path,
                                json_path = json_path,
                                thrift_port = thrift_port,
                                use_netmap = use_netmap,
                                pcap_dump = False)

        for h in xrange(2):
            host = self.addHost('h%d' % (h + 1),
                                ip = "10.0.%d.10/24" % h,
                                mac = '00:04:00:00:00:%02x' %h)
            self.addLink(host, switch, intfName1='eth0', intfName2='sweth%d'%(h))

def start_pkt_gen(net, client_name, server_name):
    h2 = net.getNodeByName(server_name)
    h1 = net.getNodeByName(client_name)

    client = h1.popen("pkt-gen -i eth0 -f tx -d %s" % (h2.IP()))

    server = h2.popen("pkt-gen -i eth0 -f rx", stdout=subprocess.PIPE)

    return client,server

# is there a need to make this function more general?
def configure_dp(commands_path, thrift_port):
    cmd = ["../tools/runtime_CLI.py",
           "--thrift-port", str(thrift_port)]
    with open(commands_path, "r") as f:
        print " ".join(cmd)
        sub_env = os.environ.copy()
        pythonpath = ""
        if "PYHTONPATH" in sub_env:
            pythonpath = sub_env["PYTHONPATH"] + ":"
        sub_env["PYTHONPATH"] = pythonpath + \
                                "../thrift_src/gen-py/"
        subprocess.Popen(cmd, stdin = f, env = sub_env).wait()

units = ['','K','M','G','T']
exps = [0,3,6,9,12]
def read_units(s):
    try:
        res = re.search(r"Speed: (\d+\.?\d*) (.?)pps", s)
        n = float(res.group(1))
        u = res.group(2)
        for unit,exp in zip(units,exps):
            if u == unit:
                n*=math.pow(10,exp)
                break
        return n
    except:
        return 0

def write_units(n):
    i=0
    if n>1:
        i = int(math.log10(n)/3)
        i = i if i<len(exps) else len(exps)-1
        n = float(n)/(math.pow(10,exps[i]))
    return str(n)+" %spps"%(units[i])

def run_measurement(net, client_name, server_name):
    c,s = start_pkt_gen(net, client_name, server_name)
    time.sleep(20)
    s.send_signal(signal.SIGINT)
    s.wait()
    c.send_signal(signal.SIGINT)
    c.wait()
    res = s.stdout.read()
    print(res)
    return read_units(res)

class MyMininet(Mininet):
    def stop(self):
        for s in self.switches:
            if isinstance(s,P4Switch):
                s.stop()
        super(MyMininet,self).stop()

def main():
    thrift_port = args.thrift_port
    num_hosts = 2

    target = args.target
    sw_path = "../targets/%s/%s" % (target,target)
    json_path = "simple_router.json"
    topo = SingleSwitchTopo(sw_path, json_path, thrift_port, args.use_netmap)
    net = MyMininet(topo = topo, host = P4Host, switch = P4Switch,
                  controller = None)
    net.start()

    sw_mac = ["00:aa:bb:00:00:%02x" % n for n in xrange(num_hosts)]

    sw_addr = ["10.0.%d.1" % n for n in xrange(num_hosts)]

    for n in xrange(num_hosts):
        h = net.get('h%d' % (n + 1))
        h.setARP(sw_addr[n], sw_mac[n])
        h.setDefaultRoute("dev eth0 via %s" % sw_addr[n])

    for n in xrange(num_hosts):
        h = net.get('h%d' % (n + 1))
        h.describe()

    sleep(1)

    configure_dp("simple_router_commands.txt", thrift_port)

    sleep(1)

    print "Ready !"

    net.pingAll()

    throughputs = []
    for i in xrange(args.repeat):
        sleep(1)
        print "Running pkt-gen measurement {} of {}".format(i + 1, args.repeat)
        t = run_measurement(net, "h1", "h2")
        throughputs.append(t)
    throughputs.sort()
    print "Median throughput is", write_units(throughputs[args.repeat / 2])
    net.stop()

if __name__ == '__main__':
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    setLogLevel( 'info' )
    main()
