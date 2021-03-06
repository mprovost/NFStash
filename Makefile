.PHONY: all clean rpcgen nfsping nfsmount nfsdf nfscat nfslock clear_locks nfsup man install

all = nfsping nfsmount nfsdf nfsls nfscat nfslock clear_locks nfsup
all: $(all) man

# installation directory
prefix = /usr/local

# check if setcap is installed
setcap := $(shell which setcap)

clean:
	rm -rf obj bin deps man/*.html man/*.8 rpcsrc/*.c rpcsrc/*.h config/*.opt config/*.out

#output directories
bin obj deps:
	mkdir -p $@

# CFLAGS borrowed from https://github.com/ggreer/the_silver_searcher
CFLAGS = -Wall -Wextra -Wformat=2 -Wshadow -Wpointer-arith -Wcast-qual -Wmissing-prototypes -Wno-missing-braces -fms-extensions -std=c99 -O2 -g -I src -I.
# rpc files have warnings about unused variables etc
# these are autogenerated so no point in seeing warnings
RPC_CFLAGS = -I.
# generate header dependencies
CPPFLAGS += -MMD -MP

HDR_CFLAGS = -Wall -Wno-unknown-pragmas -Wextra -Wshadow -Winit-self -Wmissing-prototypes -D_GNU_SOURCE -O3 -g
HDR_LIBS = -lm

# http://blog.jgc.org/2015/04/the-one-line-you-should-add-to-every.html
print-%: ; @echo $*=$($*)

# phony target to generate rpc files
# we're only really interested in the generated headers so gcc can figure out the rest of the dependencies
rpcgen: $(addprefix rpcsrc/, nfs_prot.h mount.h pmap_prot.h nlm_prot.h nfsv4_prot.h nfs_acl.h sm_inter.h rquota.h klm_prot.h)

# pattern rule for rpc files
# making this into a pattern means they are all evaluated at once which lets -j2 or higher work
# change to the rpcsrc directory first so output files go in the same directory
%.h %_clnt.c %_svc.c %_xdr.c: %.x
	rpcgen -DWANT_NFS3 $<

# pattern rule for manfiles using ronn
# unfortunately every section of the manual has a different suffix so we can't make one general rule
# the pattern rule has two targets, for roff and html

# if this is a git repo, use git log to get the last commit date of the source file
# otherwise ronn uses the mtime of the file which will change if you switch branches etc
%.8 %.html: %.8.ronn .git
	ronn --date `git log -n1 --pretty=format:%ci -- $< | cut -f1 -d" "` --style=toc $<

# if this isn't a git repo (ie building from tarball), just default to using the file mtime for the manpage dates
%.8 %.html: %.8.ronn
	ronn --style=toc $<

# create bin if it is missing
$(prefix)/bin/:
	install  -m755 -d $(prefix)/bin/

# pattern rule for installing binaries
$(prefix)/bin/%: bin/% $(prefix)/bin/
	install $< $(@D)
# use setcap if available to allow binaries to bind to reserved ports
# TODO probably don't need this capability for nfsping
# TODO maybe install nfsping in /usr/local/bin and everything else in /usr/local/sbin?
# do this after installing because install doesn't copy capability bits
ifdef setcap
	$(setcap) 'cap_net_bind_service=+ep' $(@D)/$(<F)
endif

# pattern rule for installing manpages
$(prefix)/share/man/man8/%.8: man/%.8 $(prefix)/share/man/man8/
	install -m644 $< $(@D)

# create man8 if it is missing.
$(prefix)/share/man/man8/:
	install -m755 -d $(prefix)/share/man/man8

# list of all src files for dependencies
SRC = $(wildcard src/*.c)

# pattern rule to build objects
# make the obj directory first
# gcc will fail if the rpc headers don't exist so make sure they are generated first
obj/%.o: src/%.c | obj deps rpcgen
	gcc ${CPPFLAGS} ${CFLAGS} -MF deps/$(patsubst %.o,%.d, $(notdir $@)) -c -o $@ $<

# don't need dependencies for generated source
obj/%.o: rpcsrc/%.c | obj
	gcc ${RPC_CFLAGS} -c -o $@ $<

# rule for compiling parson
obj/parson.o: parson/parson.c | obj
	gcc ${CFLAGS} -c -o $@ $<

# hdr histogram
obj/%.o: hdr/src/%.c | obj
	gcc ${HDR_CFLAGS} -c -o $@ $<

# config - check for clock_gettime in libc or librt
# this opt file gets included as a gcc option
config/clock_gettime.opt:
	cd config && ./clock_gettime.sh

# common object files
common_objs = $(addsuffix .o, pmap_prot_clnt pmap_prot_xdr util rpc parson hdr_histogram)

# make the bin directory first if it's not already there
nfsping: bin/nfsping
nfsping_objs = $(addprefix obj/, $(addsuffix .o, nfsping nfs_prot_clnt nfs_prot_xdr nfsv4_prot_clnt nfsv4_prot_xdr mount_clnt mount_xdr nlm_prot_clnt nlm_prot_xdr nfs_acl_clnt sm_inter_clnt sm_inter_xdr rquota_clnt rquota_xdr klm_prot_clnt klm_prot_xdr) $(common_objs))
bin/nfsping: config/clock_gettime.opt $(nfsping_objs) | bin
	gcc ${CFLAGS} @config/clock_gettime.opt $(nfsping_objs) ${HDR_LIBS} -o $@

nfsmount: bin/nfsmount
nfsmount_objs = $(addprefix obj/, $(addsuffix .o, mount mount_clnt mount_xdr) $(common_objs))
bin/nfsmount: config/clock_gettime.opt $(nfsmount_objs) | bin
	gcc ${CFLAGS} @config/clock_gettime.opt $(nfsmount_objs) ${HDR_LIBS} -o $@

nfsdf: bin/nfsdf
nfsdf_objs = $(addprefix obj/, $(addsuffix .o, df human nfs_prot_clnt nfs_prot_xdr) $(common_objs))
bin/nfsdf: config/clock_gettime.opt $(nfsdf_objs) | bin
	gcc ${CFLAGS} @config/clock_gettime.opt $(nfsdf_objs) ${HDR_LIBS} -o $@

nfsls: bin/nfsls
nfsls_objs = $(addprefix obj/, $(addsuffix .o, ls human nfs_prot_clnt nfs_prot_xdr xdr_copy) $(common_objs))
bin/nfsls: config/clock_gettime.opt $(nfsls_objs) | bin
    # needs math library for log10() etc
	gcc ${CFLAGS} @config/clock_gettime.opt -lm $(nfsls_objs) ${HDR_LIBS} -o $@

nfscat: bin/nfscat
nfscat_objs = $(addprefix obj/, $(addsuffix .o, cat nfs_prot_clnt nfs_prot_xdr) $(common_objs))
bin/nfscat: config/clock_gettime.opt $(nfscat_objs) | bin
	gcc ${CFLAGS} @config/clock_gettime.opt $(nfscat_objs) ${HDR_LIBS} -o $@

nfslock: bin/nfslock
nfslock_objs = $(addprefix obj/, $(addsuffix .o, lock nlm_prot_clnt nlm_prot_xdr) $(common_objs))
bin/nfslock: config/clock_gettime.opt $(nfslock_objs) | bin
	gcc ${CFLAGS} @config/clock_gettime.opt $(nfslock_objs) ${HDR_LIBS} -o $@

clear_locks: bin/clear_locks
clear_locks_objs = $(addprefix obj/, $(addsuffix .o, clear_locks sm_inter_clnt sm_inter_xdr nlm_prot_clnt nlm_prot_xdr) $(common_objs))
bin/clear_locks: config/clock_gettime.opt $(clear_locks_objs) | bin
	gcc ${CFLAGS} @config/clock_gettime.opt $(clear_locks_objs) ${HDR_LIBS} -o $@

nfsup: bin/nfsup
nfsup_objs = $(addprefix obj/, $(addsuffix .o, nfsup mount_clnt mount_xdr nfs_prot_clnt nfs_prot_xdr) $(common_objs))
bin/nfsup: config/clock_gettime.opt $(nfsup_objs) | bin
	gcc ${CFLAGS} @config/clock_gettime.opt $(nfsup_objs) ${HDR_LIBS} -o $@

tests: tests/util_tests
tests/util_tests: tests/util_tests.c tests/minunit.h src/util.o obj/parson.o obj/hdr_histogram.o src/util.h | rpcgen
	gcc ${CFLAGS} tests/util_tests.c obj/util.o obj/parson.o obj/hdr_histogram.o ${HDR_LIBS} -o $@
	tests/util_tests

# man pages
man: $(addprefix man/, $(addsuffix .8, nfsping nfsdf nfsls nfsmount nfslock nfscat clear_locks))

# quick install
install: $(addprefix $(prefix)/bin/, $(all)) $(addsuffix .8, $(addprefix $(prefix)/share/man/man8/, $(all)))

# include generated dependency files
# TODO also don't include for man target (others?)
ifneq ($(MAKECMDGOALS),clean)
-include $(SRC:src/%.c=deps/%.d)
endif
