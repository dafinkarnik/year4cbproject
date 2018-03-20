#!/usr/bin/python
"""
WebRTC development environment with a Mininet topology connected to the Internet via NAT through eth0 on the host to simulate realistic behaviour.
Headless version
Author: Dafin Kozarev

The functions startNAT, stopNAT, fixNetworkManager and connectToInternet were sourced with small modifications from Glen Gibb and are available at https://gist.github.com/hwchiu/886d9b79af5a621b36ec.
The function sshd was sampled from the official Mininet examples repository and is available at https://github.com/mininet/mininet/blob/master/examples/sshd.py.
"""

import re
import sys
import os.path
import time
import os
import datetime
from shlex import split
from subprocess import call, Popen, PIPE


from mininet.cli import CLI
from mininet.log import setLogLevel, info, error
from mininet.net import Mininet
from mininet.link import Intf
from mininet.topolib import TreeTopo
from mininet.util import quietRun
from mininet.topo import Topo
from mininet.node import CPULimitedHost, Node
from mininet.link import TCLink
from mininet.util import dumpNodeConnections, waitListening

     
class RTCTopo( Topo ):
    def __init__(self):
        # Initialize topology
        Topo.__init__( self )
        
        # Add hosts and switches
        switchL = self.addSwitch('switch1')
        switchR = self.addSwitch('switch2')
        serverRTC = self.addHost('serverRTC')
        clientL = self.addHost('clientL')
        clientR = self.addHost('clientR')
        crossServer = self.addHost('cServer')
        crossClient = self.addHost('cClient')
        
        # Add links
        self.addLink(serverRTC, switchL)
        self.addLink(clientL, switchL)
        self.addLink(clientR, switchR)
        self.addLink(crossServer, switchR)        
        self.addLink(crossClient, switchL)
        
        # Adjust network conditions here
        delay = "0ms"
        queue_size = 120
        print delay, queue_size
        self.addLink(switchL, switchR, bw=200, delay=delay, max_queue_size=queue_size)

def sshd( network, cmd='/usr/sbin/sshd', opts='-D' ):
    """Run sshd on all hosts."""

    for host in network.hosts:
        host.cmd( cmd + ' ' + opts + '&' )
    info( "*** Waiting for ssh daemons to start\nIf the testbed hangs here Ctrl+C and re-run the Python script.\n" )
    for server in network.hosts:
        waitListening( server=server, port=22, timeout=5 )

    info( "\n*** Hosts are running sshd at the following addresses:\n" )
    for host in network.hosts:
        info( host.name, host.IP(), '\n' )
    info( "\n*** Type 'exit' or control-D to shut down network\n" )

#################################
def startNAT( root, inetIntf='eth0', subnet='10.0/8' ):
    """Start NAT/forwarding between Mininet and external network
    root: node to access iptables from
    inetIntf: interface for internet access
    subnet: Mininet subnet (default 10.0/8)="""

    # Identify the interface connecting to the Mininet network
    localIntf =  root.defaultIntf()

    # Flush any currently active rules
    root.cmd( 'iptables -F' )
    root.cmd( 'iptables -t nat -F' )

    # Create default entries for unmatched traffic
    root.cmd( 'iptables -P INPUT ACCEPT' )
    root.cmd( 'iptables -P OUTPUT ACCEPT' )
    root.cmd( 'iptables -P FORWARD DROP' )

    # Configure NAT
    root.cmd( 'iptables -I FORWARD -i', localIntf, '-d', subnet, '-j DROP' )
    root.cmd( 'iptables -A FORWARD -i', localIntf, '-s', subnet, '-j ACCEPT' )
    root.cmd( 'iptables -A FORWARD -i', inetIntf, '-d', subnet, '-j ACCEPT' )
    root.cmd( 'iptables -t nat -A POSTROUTING -o ', inetIntf, '-j MASQUERADE' )

    # Instruct the kernel to perform forwarding
    root.cmd( 'sysctl net.ipv4.ip_forward=1' )
    
def stopNAT( root ):
    """Stop NAT/forwarding between Mininet and external network"""
    # Flush any currently active rules
    root.cmd( 'iptables -F' )
    root.cmd( 'iptables -t nat -F' )

    # Instruct the kernel to stop forwarding
    root.cmd( 'sysctl net.ipv4.ip_forward=0' )
    
def fixNetworkManager( root, intf ):
    """Prevent network-manager from messing with our interface,
       by specifying manual configuration in /etc/network/interfaces
       root: a node in the root namespace (for running commands)
       intf: interface name"""
    cfile = '/etc/network/interfaces'
    line = '\niface %s inet manual\n' % intf
    config = open( cfile ).read()
    if ( line ) not in config:
        print '*** Adding', line.strip(), 'to', cfile
        with open( cfile, 'a' ) as f:
            f.write( line )
    # Probably need to restart network-manager to be safe -
    # hopefully this won't disconnect you
    root.cmd( 'service network-manager restart' )
    
def connectToInternet( network, switch='switch1', rootip='10.254', subnet='10.0/8'):
    """Connect the network to the internet
       switch: switch to connect to root namespace
       rootip: address for interface in root namespace
       subnet: Mininet subnet"""
    switch = network.get( switch )
    prefixLen = subnet.split( '/' )[ 1 ]
    routes = [ subnet ]  # host networks to route to

    # Create a node in root namespace
    root = Node( 'root', inNamespace=False )

    # Prevent network-manager from interfering with our interface
    fixNetworkManager( root, 'root-eth0' )

    # Create link between root NS and switch
    link = network.addLink( root, switch )
    link.intf1.setIP( rootip, prefixLen )

    # Start network that now includes link to root namespace
    network.start()

    # Start NAT and establish forwarding
    startNAT( root )

    # Establish routes from end hosts
    for host in network.hosts:
        host.cmd( 'ip route flush root 0/0' )
        host.cmd( 'route add -net', subnet, 'dev', host.defaultIntf() )
        host.cmd( 'route add default gw', rootip )

    # Add Google DNS servers to hosts
    for host in network.hosts:
        host.cmd( "sed -i 's/nameserver.*/nameserver 8.8.8.8' /etc/resolv.conf" )
    return root
    
def main():
    # Perform cleanup in case the previous run crashed
    os.system("sudo /usr/local/bin/mn -c")
    # Set log level
    setLogLevel( 'info')
    # Get options
    argvopts = ' '.join( sys.argv[ 1: ] ) if len( sys.argv ) > 1 else (
        '-D -o UseDNS=no -u0' )
    # Load the topology
    topo = RTCTopo()
    # Start the topology in Mininet
    net = Mininet(topo, host = CPULimitedHost, link=TCLink, autoPinCpus = True)
    # Connect the network to the Internet
    rootnode = connectToInternet( net )
    # Run ssh client on all hosts
    sshd( net, opts=argvopts )
    # Sleep for 2 seconds
    time.sleep(2)
    # Run the signalling server
    call("/home/vagrant/run_signal_server.sh")

    # Generate cross traffic
    cClient = net.get('cClient')
    cClient.cmd('/usr/bin/rsync -ratlz --rsh="/usr/bin/sshpass -p vagrant ssh -o StrictHostKeyChecking=no -l vagrant" 10.0.0.2:/home/vagrant/chromium.tar.gz /home/vagrant/tmp/ --progress &')
    # If the host does not have enough resources this will affect the entire testbed and cause it to function improperly as the Chromium clients load too slowly. 
    # To counter this adjust the timer around line 196.
    
    # Start both Chromium clients with appropriate flags and navigate to the application. The call will be set-up automatically.
    clientL = net.get('clientL')
    clientL.cmd('sudo /home/vagrant/Default/chrome --headless --remote-debugging-port=8792 --remote-debugging-address=0.0.0.0 --allow-file-access-from-files --use-fake-ui-for-media-stream --use-fake-device-for-media-stream --use-file-for-fake-video-capture="/home/vagrant/akiyo_qcif.y4m" --enable-logging --vmodule=*/webrtc/*=1 --user-data-dir=/home/vagrant/profiles --no-default-browser-check --no-first-run --ignore-certificate-errors --disable-renderer-backgrounding --disable-background-timer-throttling --no-sandbox file:///home/vagrant/code/rtcclient/c2/index.html &')
    
    clientR = net.get('clientR')
    clientR.cmd('sudo /home/vagrant/Default/chrome --headless --remote-debugging-port=8793 --remote-debugging-address=0.0.0.0 --allow-file-access-from-files --use-fake-ui-for-media-stream --use-fake-device-for-media-stream --use-file-for-fake-video-capture="/home/vagrant/akiyo_qcif.y4m" --enable-logging --vmodule=*/webrtc/*=1 --temp-profile --no-default-browser-check --no-first-run --no-sandbox file:///home/vagrant/code/rtcclient/c1/index.html &')
    
    # Let the call run for a bit
    time.sleep(90)
    # Get the current date and time
    date = datetime.datetime.now().strftime("%Y_%m_%d_%H_%M_%S")
    # Create a log cb_triggered_datetime.log containing only information about triggered Circuit Breaker rules
    # Create a log cb_full_datetime.log containing the full Circuit Breaker log during execution
    filename_triggered = "/vagrant/cb_triggered_"+date+".log"
    filename_full = "/vagrant/cb_full_"+date+".log"
    os.system('touch ' + filename_triggered)
    os.system('touch ' + filename_full)
    os.system("grep '!!!' /home/vagrant/Default/chrome_debug.log > " + filename_triggered)
    os.system("grep circuit_breaker /home/vagrant/Default/chrome_debug.log > " + filename_full)
    # Note: Sometimes both clients write to chrome_debug.log simultaneously and as a result duplicate entries can be present in the logs with different timestamps.

    # Give control back to the user
    CLI( net )         

    # Clean-up if the cross-traffic file got transferred
    os.system('rm -f /home/vagrant/tmp/chromium.tar.gz')
    # Shut down SSH
    print '*** Stopping SSH on hosts'
    cmd='/usr/sbin/sshd'
    for host in net.hosts:
        info( host.name, host.IP(), '\n' )
        host.cmd( 'kill %' + cmd )

    # Shut down NAT
    stopNAT( rootnode )
    net.stop()    

if __name__ == '__main__':
    main()