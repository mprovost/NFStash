# NFSping

![nfsping](gif/dumpy.gif)

NFSping is an open source command line utility for Linux and other POSIX operating systems which measures the availability and response times of an NFS server by sending probe packets. It's based on the [fping](https://github.com/schweikert/fping) program's interface (but doesn't share any code with that project).

On modern NFS servers, the network stack and filesystem are often running on separate cores or even hardware components. In practise this means that a low latency ICMP echo (ping) response isn't indicative of how quickly the NFS services are responding. Some protocols used by NFS are typically implemented directly in the operating system's kernel while others may be handled by (often single-threaded) userland processes and can exhibit widely varying response times. NFSping allows you to directly test the responsiveness of each of a server's NFS components.

NFSping checks if each target server is responding by sending it a NULL RPC request and waiting for a response. The NULL procedure of each RPC protocol is a noop that is implemented for testing. It doesn't check any server functionality but confirms that the RPC service is listening and processing requests, and provides baseline performance information about how quickly it's responding to clients. A low latency response to a NULL request does not mean that more complex protocol requests will also respond quickly, but a high latency response to a NULL request typically indicates that more complex procedures would also take at least that much time to respond. Therefore high response times from NFSping are a reliable metric for determining when an NFS server is exhibiting performance problems.

NFSping supports several different output formats which makes it ideal for sending the response time data to be recorded and graphed by programs such as [Smokeping](https://oss.oetiker.ch/smokeping/), [Graphite](https://github.com/graphite-project/graphite-web) or [StatsD](https://github.com/etsy/statsd).

## Features
- Supports all seven of the RPC protocols that are used by the Network File Service
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
  - histogram display using [HDRHistogram](https://hdrhistogram.github.io/HdrHistogram/) (default)
  - `fping` ([Smokeping](https://oss.oetiker.ch/smokeping/) compatible) (`-C`)
  - [Graphite (Carbon)](https://github.com/graphite-project/carbon) compatible (`-G`)
  - [Etsy's StatsD](https://github.com/etsy/statsd) compatible (`-E`)
- [Hash-Cast](#hash-cast) avoidance

## Usage

The manual page for NFSping is available [here](https://rawgit.com/mprovost/NFStash/master/man/nfsping.8.html).

```console
Usage: nfsping [options] [targets...]
    -a         check the NFS ACL protocol (default NFS)
    -A         show IP addresses (default hostnames)
    -c n       count of pings to send to target
    -C n       same as -c, output parseable format
    -d         reverse DNS lookups for targets
    -D         print timestamp (unix time) before each line
    -E         StatsD format output (default human readable)
    -g string  prefix for Graphite/StatsD metric names (default "nfsping")
    -G         Graphite format output (default human readable)
    -h         display this help and exit
    -H n       frequency in Hertz (pings per second, default 10)
    -i n       interval between sending packets (in ms, default 1)
    -K         check the kernel lock manager (KLM) protocol (default NFS)
    -l         loop forever (default)
    -L         check the network lock manager (NLM) protocol (default NFS)
    -m         use multiple target IP addresses if found (implies -A)
    -M         use the portmapper (default: NFS/ACL no, mount/NLM/NSM/rquota yes)
    -n         check the mount protocol (default NFS)
    -N         check the portmap protocol (default NFS)
    -P n       specify port (default: NFS 2049, portmap 111)
    -q         quiet, only print summary
    -Q n       same as -q, but show summary every n seconds
    -R         don't reconnect to server every ping
    -s         check the network status monitor (NSM) protocol (default NFS)
    -S addr    set source address
    -t n       timeout (in ms, default 1000)
    -T         use TCP (default UDP)
    -u         check the rquota protocol (default NFS)
    -v         verbose output
    -V n       specify NFS version (2/3/4, default 3)
```

NFSping sends NULL RPCs to each target server at a specific frequency, 10 times per second by default. This can be changed with the `-H` option to specify a frequency (in Hertz), so for example `-H 100` will send 100 pings per second (every 10ms). Polling on a fixed frequency allows NFSping to send data to time series databases that expect updates on a regular basis.

The `-Q` option sets the interval in seconds to print a histogram report of response times. A one line histogram summary of responses is printed for each interval, and finally a cumulative full HDR histogram for all responses is output when exiting. This enables using a higher frequency polling interval with `-H` while minimising the number of lines of output. For example, to keep the default polling frequency of 10 Hz (or higher) while emulating ping's historic behaviour of printing a line of output every second, use `-Q 1`.

NFSping supports versions 2, 3 and 4 of NFS, and the corresponding versions of the other RPC protocols. With no arguments it will send NFS version 3 NULL requests. By default it doesn't use the RPC portmapper for NFS and connects to UDP port 2049, which is the standard port for NFS. Specify the `-T` option to use TCP, `-M` to query the portmapper for the server's NFS port, or `-P` to specify a port number. The `-V` option can be used to select another version of the protocol (2 or 4).
perform
It's not possible to individually check minor versions of NFS version 4 (4.1, 4.2 etc) because they are all implemented under the same RPC protocol number. The version 4 protocol only has two procedures - NULL and COMPOUND. The COMPOUND procedure requires that each call specify a minor version, but the NULL procedure lacks this argument.

NFSping can also check the mount protocol response using the `-n` option. This protocol is used in NFS versions 2 and 3 to look up a filesystem's root filehandle on the server when a client mounts a remote filesystem. In version 4 this functionality has been built into the NFS protocol itself. By default NFSping uses the portmapper to discover the port that the mount protocol is listening to on the target (there is no standard port, although server vendors typically select a stable port). Use the `-P` option to specify a port.

The `-L` option will check the network lock manager (NLM) protocol. The lock manager is a stateful protocol for managing file locks that was added to NFS version 2 and 3 servers - in version 4 locking has been built into the NFS protocol itself. By default NFSping uses the portmapper to discover the port that the NLM protocol is listening to on the target. Use the `-P` option to specify a port.

The `-N` option will check the portmap protocol itself (always listening on port 111). This is also called the portmapper or rpcbind service. The portmap protocol is used by clients to look up which port a specific RPC service and version pair is bound to on a server.

The `-a` option will check the NFS ACL protocol which usually listens on port 2049 alongside NFS but is a separate RPC protocol. This is a "sideband" protocol that Sun created for manipulating POSIX ACLs to that was never standardised in an RFC. However several servers such as Solaris and Linux implement it. By default NFSping checks for it on port 2049, specify a port with `-P` or query the portmapper with `-M`.

The `-s` option will check the network status monitor (NSM) protocol which is used to notify NFS peers of host reboots. It shows up as the "status" protocol in rpcinfo output. It forms part of the stateful locking protocol so that stale locks can be cleared when a server reboots. NFS version 4 has locking built in. By default NFSping uses the portmapper to discover the port that the NSM protocol is listening to on the target. Use the `-P` option to specify a port.

The `-u` option will check the rquota protocol which is used by clients to check and set remote quotas on the server. By default NFSping uses the portmapper to discover the port that the rquota protocol is listening to on the target. Use the `-P` option to specify a port.

NFSping will handle multiple targets and iterate through them on each round. If the response to a DNS query contains multiple IP addresses for a target (such as for a clustered fileserver), only the first is used. All of them can be checked by using the `-m` option. If any of the targets fail to respond, the command will exit with a status code of 1.

Because nfsping is single threaded, unresponsive NFS servers will timeout and may cause the polling round to overrun when specifying multiple targets (or a single target with multiple IP addresses using the `-m` option). It's recommended to run one command per NFS server or cluster to avoid this affecting all monitored hosts.

NFSping only performs DNS lookups once during initialisation. If an NFS server's IP addresses change (for example if it's a clustered server and you are using the `-m` option to resolve multiple addresses), consider using the `-c` option to exit after sending a certain number of requests and running NFSping under a process supervisor like [daemontools](http://cr.yp.to/daemontools.html) or [runit](http://smarden.org/runit/) which will restart the process if it exits for any reason, when it will perform the DNS lookups again.

## Hash-Cast
NFS servers can exhibit varied response times for different TCP connections. Some client connections will exhibit consistently low response times while others may have much higher ones. This winner-loser pattern has been named Hash-Cast by Chen et al in ["Newer Is Sometimes Better: An Evaluation of NFSv4.1"](https://www.fsl.cs.sunysb.edu/docs/nfs4perf/nfs4perf-sigm15.pdf). They identified the cause of this pattern as the operating system unevenly hashing TCP flows onto different transmit queues on a NIC. Flows that are hashed onto a busy queue will show consistently higher latency. The symptom of a server suffering from hash cast is that NFSping will report a bimodal distribution with two clusters of results, one consistently higher than the other.

To avoid being hashed to a single queue on the server, NFSping reconnects to the server after every ping, using a different local port each time. (Hashes are calculated using the source and destination IP address and port). This doesn't guarantee that the new connection will be assigned to a different queue (if there are 4 queues on the server's NIC, there is still a 25% chance of hitting the same queue again) but over multiple pings the probability that all queues will be hit approaches certainty. To disable this behaviour and keep reusing the same connection to each server, use the `-R` option. Note that connecting to the server is done before the time measurement for the NULL request so it doesn't increase any of the reported response times.

## Security
NFSping uses the `AUTH_NONE` authentication flavour which doesn't send any user information. Some NFS servers can be configured to require client connections from privileged ports (< 1024), however according to [RFC 2623](https://tools.ietf.org/html/rfc2623#section-2.3.1) servers shouldn't require these "secure" ports for the NULL procedure. If a server is found that requires authentication or secure ports, please open an issue [here](https://github.com/mprovost/NFStash/issues/new).

## Examples

Without any arguments, NFSping runs sending RPCs in a loop until it's interrupted with `control-c`. A specific number of requests can be sent with the `-c` argument. For each result, it prints the round trip time (RTT), the minimum RTT from all results, the 50/90/99th percentiles of all round trip times, and the maximum RTT. All of these are in milliseconds (with microsecond precision). After it's interrupted, an HDR Histogram summary report is printed showing the percentiles for all results.

```console
$ nfsping dumpy
{nfsv3      RTT     min     p50     p90     p99     max
dumpy :   0.911   0.911   0.911   0.911   0.911   0.911 ms
dumpy :   0.979   0.911   0.911   0.979   0.979   0.979 ms
dumpy :   1.068   0.911   0.979   1.068   1.068   1.068 ms
dumpy :   1.140   0.911   0.979   1.140   1.140   1.140 ms
dumpy :   1.078   0.911   1.068   1.140   1.140   1.140 ms

dumpy :
       Value   Percentile   TotalCount 1/(1-Percentile)
       0.911     0.000000            1         1.00
       0.911     0.100000            1         1.11
       0.911     0.200000            1         1.25
       0.979     0.300000            2         1.43
       0.979     0.400000            2         1.67
       1.068     0.500000            3         2.00
       1.068     0.550000            3         2.22
       1.068     0.600000            3         2.50
       1.078     0.650000            4         2.86
       1.078     0.700000            4         3.33
       1.078     0.750000            4         4.00
       1.078     0.775000            4         4.44
       1.078     0.800000            4         5.00
       1.140     0.825000            5         5.71
       1.140     1.000000            5          inf
#[Mean    =        1.035, StdDeviation   =        0.081]
#[Max     =        1.140, Total count    =            5]
#[Buckets =           10, SubBuckets     =         2048]
```

To exit early in any mode, use `control-c`.

NFSping also has an fping compatible form that produces easily parseable output with the `-C` option:

```console
$ nfsping -C 5 dumpy
dumpy : [0], 1.96 ms (1.96 avg, 0% loss)
dumpy : [1], 0.11 ms (1.04 avg, 0% loss)
dumpy : [2], 0.12 ms (0.73 avg, 0% loss)
dumpy : [3], 0.16 ms (0.59 avg, 0% loss)
dumpy : [4], 0.18 ms (0.51 avg, 0% loss)

dumpy : 1.96 0.11 0.12 0.16 0.18
```

Missed responses are indicated with a dash (-) in the summary output. This form uses more memory since it stores all of the results. In all other forms memory is allocated during startup so there should be no increase in memory consumption once running. The `-C` format is compatible with fping's output so it can be easily used with Tobi Oetiker's [Smokeping](http://oss.oetiker.ch/smokeping/) to produce graphs of response times. There is a module for NFSPing in the standard Smokeping distribution or in the Smokeping subdirectory of the NFSping source.

To only show the summary line, use the `-q` (quiet) option.

NFSping can also output stats in Graphite (`-G`) and Etsy's StatsD (`-E`) formats for inserting into time series databases.

```console
$ nfsping -c 5 -G dumpy
nfsping.dumpy.ping.usec 401 1370501562
nfsping.dumpy.ping.usec 416 1370501563
nfsping.dumpy.ping.usec 403 1370501564
nfsping.dumpy.ping.usec 410 1370501565
nfsping.dumpy.ping.usec 399 1370501566
```

This uses the Graphite plaintext protocol which is `<path> <metric> <timestamp>`. To avoid floating point numbers, nfsping reports the response time in microseconds (usec).

The default prefix for the Graphite path is "nfsping". This can be changed by specifying a new string as an argument to the `-g` option. Fully qualified domain names (but not IP addresses) for targets will be reversed:

```console
$ nfsping -c 1 -G -g filers dumpy.my.domain
filers.domain.my.dumpy.ping.usec 292 1409332974
```

Lost requests (or timeouts) will be reported under a separate path, $prefix.$target.$protocol.lost, with a metric of 1.

This output can be easily piped to a Carbon server using nc (netcat):

```console
$ nfsping -l -G dumpy filer2 | nc carbon1 2003
```

nc will exit if the TCP connection is reset (such as if the Carbon server is restarted) which will also cause nfsping to exit with a broken pipe. To automatically reconnect, start it under a process supervisor which will restart it when it exits for any reason.

Similarly, the StatsD output will produce plaintext output suitable for sending to StatsD with netcat:

```console
$ nfsping -c 5 -E dumpy
nfsping.dumpy.ping:0.15|ms
nfsping.dumpy.ping:0.18|ms
nfsping.dumpy.ping:3.27|ms
nfsping.dumpy.ping:11.61|ms
nfsping.dumpy.ping:78.07|ms
```

Note that this output uses floating point values, as the StatsD protocol only supports milliseconds. While floating point values are supported by the protocol, some implementations may not handle them. This output has been tested as working with [statsite](https://github.com/armon/statsite).
