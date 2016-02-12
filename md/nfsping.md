# NFSping

```console
$ nfsping -c 5 filer1 filer2
filer1 : [0], 0.52 ms (0.52 avg, 0% loss)
filer2 : [0], 1.12 ms (1.12 avg, 0% loss)
filer1 : [1], 0.35 ms (0.43 avg, 0% loss)
filer2 : [0], 0.98 ms (1.05 avg, 0% loss)
filer1 : [2], 0.36 ms (0.41 avg, 0% loss)
filer2 : [0], 1.67 ms (1.26 avg, 0% loss)
filer1 : [3], 1.18 ms (0.60 avg, 0% loss)
filer2 : [0], 1.23 ms (1.25 avg, 0% loss)
filer1 : [4], 0.33 ms (0.55 avg, 0% loss)
filer2 : [0], 1.36 ms (1.27 avg, 0% loss)

filer1 : xmt/rcv/%loss = 5/5/0%, min/avg/max = 0.33/0.55/1.18
filer2 : xmt/rcv/%loss = 5/5/0%, min/avg/max = 0.98/1.27/1.67
```

NFSping is an open source command line utility for Linux and other POSIX operating systems which measures the availability and response times of an NFS server by sending probe packets. It's based on the [fping](https://github.com/schweikert/fping) program's interface (but doesn't share any code with that project).

On modern NFS servers, the network stack and filesystem are often running on separate cores or even hardware components. In practise this means that a fast ICMP ping response isn't indicative of how quickly the NFS services are responding. This tool directly tests the responsiveness of the server's NFS components. 

NFSping checks if each target server is responding by sending it a NULL RPC request and waiting for a response. The NULL procedure of each RPC protocol is a noop that is implemented for testing. It doesn't check any server functionality but provides confirmation that the RPC service is listening and processing requests, and baseline performance information about how quickly it's responding. A fast response to a NULL request does not mean that more complex protocol requests will also respond quickly, but a slow response to a NULL request typically indicates that more complex procedures would also take at least that much time to respond. Therefore high response times from NFSping are a reliable metric for determining when an NFS server is exhibiting performance problems.

NFSping supports several different output formats which makes it ideal for sending the response time data to be recorded and graphed by programs such as [Smokeping](https://oss.oetiker.ch/smokeping/) or [Graphite](https://github.com/graphite-project/graphite-web).

## Features
- Supports all seven of the RPC protocols that are used by NFS
  - NFS - versions 2/3/4
  - mount
  - portmap (rpcbind)
  - network lock manager (NLM)
  - Sun's ACL sideband protocol
  - network status monitor (NSM)
  - rquota
  - Also supports the kernel lock manager (KLM) protocol used by some kernels to communicate with a userspace locking daemon
- TCP and UDP probes
- Various output formats
    - traditional `ping` (default)
    - timestamped `ping` (`-D`)
    - `fping` ([Smokeping](https://oss.oetiker.ch/smokeping/) compatible) (`-C`)
    - [Graphite (Carbon)](https://github.com/graphite-project/carbon) compatible (`-G`)
    - [Etsy's StatsD](https://github.com/etsy/statsd) compatible (`-E`)
- [Hash-Cast](#hash-cast) avoidance

NFSping sends pings to each target regularly at a specific frequency, once per second by default. This can be changed with the `-H` option to specify a higher frequency (in Hertz), so for example `-H 10` will send 10 pings per second (every 100ms). Polling on a fixed frequency allows NFSping to send data to monitoring systems that expect updates on a regular basis.

NFSping supports versions 2, 3 and 4 of NFS, and the corresponding versions of the other RPC protocols. With no arguments it will send NFS version 3 NULL requests using UDP. By default it doesn't use the RPC portmapper for NFS pings and connects to port 2049, which is the standard port for NFS. Specify the `-T` option to use TCP, `-M` to query the portmapper for the server's NFS port, or `-P` to specify a port number. The `-V` option can be used to select another version of the protocol (2 or 4).

Minor versions of NFS version 4 (4.1, 4.2 etc) aren't able to be checked individually because they are all implemented under the same RPC protocol number. The version 4 protocol only has two procedures - NULL and COMPOUND. The COMPOUND procedure requires that each call specify a minor version, but the NULL procedure doesn't have an argument for minor versions.

NFSping can also check the mount protocol response using the `-n` option. This protocol is used in NFS version 2 and 3 to look up the root filehandle for a specific mount point on the server, which is then used by the NFS client. In version 4 this functionality has been built into the NFS protocol itself. By default NFSping uses the portmapper to discover the port that the mount protocol is listening to on the target. Use the `-P` option to specify a port.

The `-L` option will check the network lock manager (NLM) protocol. The lock manager is a stateful protocol for managing file locks that was added to NFS version 2 and 3 servers - in version 4 locking has been built into the NFS protocol itself. By default NFSping uses the portmapper to discover the port that the NLM protocol is listening to on the target. Use the `-P` option to specify a port.

The `-N` option will check the portmap protocol itself (always listening on port 111). This is also called the portmapper or rpcbind service. The portmap protocol is used by clients to look up which port a particular RPC service and version is bound to on a server.

The `-a` option will check the NFS ACL protocol which usually listens on port 2049 alongside NFS but is a separate RPC protocol. This is a "sideband" protocol that Sun created for manipulating POSIX ACLs to that was never standardised in an RFC. However several servers such as Solaris and Linux implement it. By default NFSping checks for it on port 2049, specify a port with `-P`.

The `-s` option will check the network status monitor (NSM) protocol which is used to notify NFS peers of host reboots. It shows up as the "status" protocol in rpcinfo output. It forms part of the stateful locking protocol so that stale locks can be cleared when a server reboots. NFS version 4 has locking built in. By default NFSping uses the portmapper to discover the port that the NSM protocol is listening to on the target. Use the `-P` option to specify a port.

The `-Q` option will check the rquota protocol which is used by clients to check and set remote quotas on the server. By default NFSping uses the portmapper to discover the port that the rquota protocol is listening to on the target. Use the `-P` option to specify a port.

NFSping will handle multiple targets and iterate through them on each round. If there are multiple DNS responses for a target, only the first is used. All of them can be checked by using the `-m` option. The interval (delay) between targets can be controlled with the `-i` option, usually this can be quite short and defaults to 25ms. If any of the targets fail to respond, the command will exit with a status code of 1.

NFSping only performs DNS lookups once during initialisation. If the NFS server's IP addresses change (for example if it's a clustered server and you are using the `-m` option to resolve multiple addresses), consider using the `-c` option to exit after sending a certain number of pings and running NFSping under a process supervisor like [daemontools](http://cr.yp.to/daemontools.html) or [runit](http://smarden.org/runit/) which will restart the process if it exits for any reason, when it will do the DNS lookups again.

## Hash-Cast
NFS servers can exhibit varied response times for different TCP connections. Some connections will exhibit consistently low response times while others will have much higher ones. This winner-loser pattern has been named Hash-Cast by Chen et al in ["Newer Is Sometimes Better: An Evaluation of NFSv4.1"](https://www.fsl.cs.sunysb.edu/docs/nfs4perf/nfs4perf-sigm15.pdf). They identified this pattern as being caused by the operating system unevenly hashing TCP flows onto different transmit queues on a NIC. Flows that are hashed onto a busy queue will show consistently higher latency. The symptom of a server suffering from hash cast is that NFSping will report a bimodal distribution with two clusters of results, one consistently higher than the other.

To avoid being hashed to a single queue on the server, NFSping reconnects to the server after every ping, using a different local port each time. This doesn't guarantee that the new connection will be assigned to a new queue (if there are 4 queues on the server's NIC, there is still a 25% chance of hitting the same queue again) but over multiple pings the probability that all queues will be hit approaches certainty. To disable this behaviour and keep reusing the same connection to each server, use the `-R` option. Note that connecting to the server is done before the time measurement for the NULL request so it doesn't affect any of the reported response times.

## Security
NFSping uses the `AUTH_NONE` authentication flavour which doesn't send any user information. Some NFS servers can be configured to require client connections from privileged ports (< 1024), however according to [RFC 2623](https://tools.ietf.org/html/rfc2623#section-2.3.1) servers shouldn't require these "secure" ports for the NULL procedure. If a server is found that requires authentication or secure ports, please open an issue [here](https://github.com/mprovost/NFSping/issues/new).

## Usage

The manual page for NFSping is available [here](https://rawgit.com/mprovost/NFStash/master/man/nfsping.8.html).

```console
Usage: nfsping [options] [targets...]
    -a         check the NFS ACL protocol (default NFS)
    -A         show IP addresses
    -c n       count of pings to send to target
    -C n       same as -c, output parseable format
    -d         reverse DNS lookups for targets
    -D         print timestamp (unix time) before each line
    -E         StatsD format output (default human readable)
    -g string  prefix for Graphite/StatsD metric names (default "nfsping")
    -G         Graphite format output (default human readable)
    -h         display this help and exit
    -H n       frequency in Hertz (pings per second, default 1)
    -i n       interval between sending packets (in ms, default 25)
    -K         check the kernel lock manager (KLM) protocol (default NFS)
    -l         loop forever
    -L         check the network lock manager (NLM) protocol (default NFS)
    -m         use multiple target IP addresses if found (implies -A)
    -M         use the portmapper (default: NFS/ACL no, mount/NLM/NSM/rquota yes)
    -n         check the mount protocol (default NFS)
    -N         check the portmap protocol (default NFS)
    -P n       specify port (default: NFS 2049, portmap 111)
    -q         quiet, only print summary
    -Q         check the rquota protocol (default NFS)
    -R         don't reconnect to server every ping
    -s         check the network status monitor (NSM) protocol (default NFS)
    -S addr    set source address
    -t n       timeout (in ms, default 1000)
    -T         use TCP (default UDP)
    -v         verbose output
    -V n       specify NFS version (2/3/4, default 3)
```

## Examples

In its most basic form it simply reports whether the server is responding to NFS requests:

```console
$ nfsping filer1
filer1 is alive
```

and exits with a return status of 0, or:

```console
$ nfsping filer1
filer1 : nfsproc3_null_3: RPC: Unable to receive; errno = Connection refused
filer1 is dead
```

and exiting with a status of 1. This simple form of the command can be built into scripts which just check if the server is up or not without being concerned about a particular response time.

To measure round trip response time (in milliseconds), pass the number of requests to send as an argument to the `-c` (count) option:

```console
$ nfsping -c 5 filer1
filer1 : [0], 0.09 ms (0.09 avg, 0% loss)
filer1 : [1], 0.16 ms (0.12 avg, 0% loss)
filer1 : [2], 0.15 ms (0.13 avg, 0% loss)
filer1 : [3], 0.16 ms (0.14 avg, 0% loss)
filer1 : [4], 0.12 ms (0.14 avg, 0% loss)

filer1 : xmt/rcv/%loss = 5/5/0%, min/avg/max = 0.09/0.14/0.16
```

Or to send a continuous sequence of packets (like the traditional ICMP ping command) use the `-l` (loop) option:

```console
$ nfsping -l filer1
```

To exit early in any mode, use `control-c`.

NFSping also has an fping compatible form that produces more easily parsed output with the `-C` option:

```console
$ nfsping -C 5 filer1
filer1 : [0], 1.96 ms (1.96 avg, 0% loss)
filer1 : [1], 0.11 ms (1.04 avg, 0% loss)
filer1 : [2], 0.12 ms (0.73 avg, 0% loss)
filer1 : [3], 0.16 ms (0.59 avg, 0% loss)
filer1 : [4], 0.18 ms (0.51 avg, 0% loss)

filer1 : 1.96 0.11 0.12 0.16 0.18
```

Missed responses are indicated with a dash (-) in the summary output. This form uses more memory since it stores all of the results. In all forms memory is allocated during startup so there should be no increase in memory consumption once running. The `-C` format is compatible with fping's output so it can be easily used with Tobi Oetiker's [Smokeping](http://oss.oetiker.ch/smokeping/) to produce graphs of response times. There is a module for NFSPing in the standard Smokeping distribution or in the Smokeping subdirectory of the NFSping source.

To only show the summary line, use the `-q` (quiet) option.

NFSping can also output stats in Graphite and Etsy's StatsD formats for inserting into time series databases.

```console
$ nfsping -c 5 -G filer1
nfsping.filer1.ping.usec 401 1370501562
nfsping.filer1.ping.usec 416 1370501563
nfsping.filer1.ping.usec 403 1370501564
nfsping.filer1.ping.usec 410 1370501565
nfsping.filer1.ping.usec 399 1370501566
```

This is the Graphite plaintext protocol which is `<path> <metric> <timestamp>`. To avoid floating point numbers, nfsping reports the response time in microseconds (usec).

The default prefix for the Graphite path is "nfsping". This can be changed by specifying a new string as an argument to the `-g` option. Fully qualified domain names (but not IP addresses) for targets will be reversed:

```console
$ nfsping -c 1 -G -g filers filer1.my.domain
filers.domain.my.filer1.ping.usec 292 1409332974
```

This output can be easily piped to a Carbon server using nc (netcat):

```console
$ nfsping -l -G filer1 filer2 | nc carbon1 2003
```

This will send a result every second. Because nfsping is single threaded, unresponsive NFS servers will timeout and may cause the polling round to overrun when specifying multiple targets. It's recommended to run one command per NFS server or cluster to avoid this affecting all monitored hosts. Lost requests (or timeouts) will be reported under a separate path, $prefix.$target.$protocol.lost, with a metric of 1.

nc will exit if the TCP connection is reset (such as if the Carbon server is restarted) which will also cause nfsping to exit with a broken pipe. To retry, start a persistent process under a process supervisor which will restart the process if it exits for any reason.

Similarly, the StatsD output will produce plaintext output suitable for sending to StatsD with netcat:

```console
$ nfsping -c 5 -E filer1
nfsping.filer1.ping:0.15|ms
nfsping.filer1.ping:0.18|ms
nfsping.filer1.ping:3.27|ms
nfsping.filer1.ping:11.61|ms
nfsping.filer1.ping:78.07|ms
```

Note that this output uses floating point values, as the StatsD protocol only supports milliseconds. While floating point values are supported by the protocol, some implementations may not handle them. This output has been tested as working with [statsite](https://github.com/armon/statsite).
