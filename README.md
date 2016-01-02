# NFStash **⏞** : CLI NFS client tools

NFStash (pronounced "en-ef stash") is a suite of **command line tools** for Linux and other POSIX operating systems which implement Network File System (NFS) client procedures. These utilities can be used for **testing, debugging, monitoring and benchmarking** the responses from an NFS server in a **composable** and **reproducible** way, either **interactively** or in **scripts**.

The suite consists of:

- [`nfsping`](md/nfsping.md): send RPC NULL requests to NFS servers
- [`nfsmount`](man/nfsmount.8.html): lookup NFS filesystem root filehandles
- [`nfsdf`](man/nfsdf.html): report NFS server disk space usage
- [`nfsls`](man/nfsls.8.html): list directory contents on an NFS server
- [`nfscat`](man/nfscat.8.html): read a file over NFS and print to stdout
- [`nfslock`](man/nfslock.8.html): test getting an NFS lock on a filehandle
- [`clear_locks`](man/clear_locks.8.html): clear file locks on an NFS server

The goal of the project is to eventually support all 22 NFS version 3 client procedures.

## Main Features
- [BSD licensed](http://opensource.org/licenses/bsd-license.php)
- Written in **C** for portability and speed
- Built from the original Sun RPC protocol files
- No requirement for any libraries other than libc (and librt for clock_gettime() if using an older version of GNU libc).
- No dependencies on the operating system's NFS client
- Easily readable and parseable **JSON** output
- Timing output compatible with [fping](https://github.com/schweikert/fping), [Graphite](https://github.com/graphite-project/graphite-web) or [StatsD](https://github.com/etsy/statsd)

## Installation

```console
$ git clone git://github.com/mprovost/NFStash.git
$ cd NFStash && make
$ sudo make install
```````

- `make install` will copy the binaries to `/usr/local/bin/` and manpages to `/usr/local/share/man/`. To change this edit the `prefix` in the Makefile.
- Requires `gmake`.
- Uses some `gcc`-isms which may mean it won't compile with other C compilers.
- Manpages are built with [`ronn`](http://rtomayko.github.io/ronn/).
- RPC code is generated with `rpcgen`.
- At the moment it doesn't compile on FreeBSD because of conflicts with the portmap header files that it generates and the builtin RPC headers shipped with FreeBSD.
- It doesn't compile on OSX yet due to a missing clock_gettime() - this will take some porting effort (probably based on sudo_clock_gettime() from sudo).
- The Makefile uses a test in the `/config` directory to check whether it needs to link the realtime library (-lrt) to pull in clock_gettime(). This is included in libc itself in glibc > 2.17.

## Roadmap
- [ ] convert internal time calculations to nanoseconds
- [ ] [HDRHistogram](https://github.com/HdrHistogram/HdrHistogram_c) support
- [ ] Workaround Coordinated Omission like [wrk2](https://github.com/giltene/wrk2)
- [ ] Fix compilation issues on *BSD
- [ ] OSX support ([clock_gettime](http://www.sudo.ws/repos/sudo/file/adf7997a0a65/lib/util/clock_gettime.c))
- [ ] Multithreaded so slow responses don't block other requests?
- [ ] A simplified version of NFSping for Nagios-compatible monitoring checks
- [ ] Simplify output formats and move output conversion to a utility
