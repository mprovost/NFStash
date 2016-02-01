# NFStash **⏞** : NFS client CLI tools

NFStash (pronounced "en-ef stash") is a suite of **command line tools** for Linux and other POSIX operating systems which implement Network File System (NFS) client procedures. These utilities can be used for **testing, debugging, monitoring and benchmarking** the responses from an NFS server in a **composable** and **reproducible** way, either **interactively** or in **scripts**.

The suite consists of these tools:

- [`nfsping`](md/nfsping.md): send RPC NULL requests to NFS servers
- [`nfsmount`](https://rawgit.com/mprovost/NFSping/master/man/nfsmount.8.html): lookup NFS filesystem root filehandles
- [`nfsdf`](https://rawgit.com/mprovost/NFSping/master/man/nfsdf.8.html): report NFS server disk space usage
- [`nfsls`](https://rawgit.com/mprovost/NFSping/master/man/nfsls.8.html): list directory contents on an NFS server
- [`nfscat`](https://rawgit.com/mprovost/NFSping/master/man/nfscat.8.html): read a file over NFS and print to stdout
- [`nfslock`](https://rawgit.com/mprovost/NFSping/master/man/nfslock.8.html): test getting an NFS lock on a filehandle
- [`clear_locks`](https://rawgit.com/mprovost/NFSping/master/man/clear_locks.8.html): clear file locks on an NFS server

The goal of the project is to eventually support all 22 NFS version 3 client procedures.

## Main Features
- Free and Open Source software ([BSD licensed](http://opensource.org/licenses/bsd-license.php))
- Written in **C** for portability and speed
- Built from the original Sun RPC protocol files
- No requirement for any libraries other than libc (and `librt` for `clock_gettime()` if using an older version of GNU libc).
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
- It doesn't compile on OSX yet due to a missing `clock_gettime()` - this will take some porting effort (probably using [monotonic_clock](https://github.com/ThomasHabets/monotonic_clock)).
- The Makefile uses a test in the `/config` directory to check whether it needs to link the realtime library (-lrt) to pull in clock_gettime(). This is included in libc itself in glibc > 2.17.

## Roadmap
NFStash is under active development and heading towards an initial stable release.

- [ ] convert internal time calculations to nanoseconds
- [ ] [HDRHistogram](https://github.com/HdrHistogram/HdrHistogram_c) support
- [ ] Workaround Coordinated Omission like [wrk2](https://github.com/giltene/wrk2)
- [ ] Fix portmap header conflicts on *BSD
- [ ] OSX support ([clock_gettime](https://github.com/ThomasHabets/monotonic_clock))
- [ ] Multithreaded so slow responses don't block other requests?
- [ ] A simplified version of NFSping for Nagios-compatible monitoring checks
- [ ] Simplify output formats and move output conversion to a utility
- [ ] Privilege separation to drop root privileges after binding to reserved ports for "secure" NFS servers

## Contributing
Patches and bug reports are welcome. If someone is interested in contributing code to the project, sponsorship for a development environment in Google Cloud Engine or Amazon Web Services is available. Please contact the author for more details.
