.PHONY: all clean rpcgen nfsping nfsmount nfsdf nfscat man install

all = nfsping nfsmount nfsdf nfsls nfscat nfslock
all: $(all) man

# installation directory
prefix = /usr/local

clean:
	rm -rf obj bin deps man rpcsrc/*.c rpcsrc/*.h

#output directories
bin obj deps man/man8:
	mkdir -p $@

# FIXME -lrt is only needed for clock_gettime() and only in glibc < 2.17
CFLAGS = -Werror -g -I src -I. -lrt
# generate header dependencies
CPPFLAGS += -MMD -MP

# http://blog.jgc.org/2015/04/the-one-line-you-should-add-to-every.html
print-%: ; @echo $*=$($*)

# phony target to generate rpc files
# we're only really interested in the generated headers so gcc can figure out the rest of the dependencies
rpcgen: $(addprefix rpcsrc/, nfs_prot.h mount.h pmap_prot.h nlm_prot.h nfsv4_prot.h nfs_acl.h sm_inter.h rquota.h)

# pattern rule for rpc files
# making this into a pattern means they are all evaluated at once which lets -j2 or higher work
# change to the rpcsrc directory first so output files go in the same directory
%.h %_clnt.c %_svc.c %_xdr.c: %.x
	rpcgen -DWANT_NFS3 $<

# pattern rule for makefiles using ronn
# unfortunately every section of the manual has a different suffix so we can't make one general rule
# create the output directory first
man/man8/%.8: mansrc/%.8.ronn | man/man8
	ronn -w -r $< --pipe > $@

# pattern rule for installing binaries
$(prefix)/bin/%: bin/%
	install $< $(@D)

# pattern rule for installing manpages
$(prefix)/share/man/man8/%: man/man8/% $(prefix)/share/man/man8/
	install -m644 $< $(@D)

# create man8 if it is missing.
$(prefix)/share/man/man8/:
	install -m655 -d $(prefix)/share/man/man8

# list of all src files for dependencies
SRC = $(wildcard src/*.c)

# pattern rule to build objects
# make the obj directory first
# gcc will fail if the rpc headers don't exist so make sure they are generated first
obj/%.o: src/%.c | obj deps rpcgen
	gcc ${CPPFLAGS} ${CFLAGS} -MF deps/$(patsubst %.o,%.d, $(notdir $@)) -c -o $@ $<

# don't need dependencies for generated source
obj/%.o: rpcsrc/%.c | obj
	gcc ${CFLAGS} -c -o $@ $<

# rule for compiling parson
obj/parson.o: parson/parson.c | obj
	gcc ${CFLAGS} -c -o $@ $<

# make the bin directory first if it's not already there
# TODO addsuffix .o
# TODO make common files into variable
nfsping: bin/nfsping
bin/nfsping: $(addprefix obj/, nfsping.o nfs_prot_clnt.o nfs_prot_xdr.o nfsv4_prot_clnt.o nfsv4_prot_xdr.o mount_clnt.o mount_xdr.o pmap_prot_clnt.o pmap_prot_xdr.o nlm_prot_clnt.o nlm_prot_xdr.o nfs_acl_clnt.o sm_inter_clnt.o sm_inter_xdr.o rquota_clnt.o rquota_xdr.o util.o rpc.o parson.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfsmount: bin/nfsmount
bin/nfsmount: $(addprefix obj/, mount.o mount_clnt.o mount_xdr.o pmap_prot_clnt.o pmap_prot_xdr.o util.o rpc.o parson.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfsdf: bin/nfsdf
bin/nfsdf: $(addprefix obj/, df.o nfs_prot_clnt.o nfs_prot_xdr.o pmap_prot_clnt.o pmap_prot_xdr.o util.o rpc.o parson.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfsls: bin/nfsls
bin/nfsls: $(addprefix obj/, ls.o nfs_prot_clnt.o nfs_prot_xdr.o pmap_prot_clnt.o pmap_prot_xdr.o util.o rpc.o parson.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfscat: bin/nfscat
bin/nfscat: $(addprefix obj/, cat.o nfs_prot_clnt.o nfs_prot_xdr.o pmap_prot_clnt.o pmap_prot_xdr.o util.o rpc.o parson.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfslock: bin/nfslock
bin/nfslock: $(addprefix obj/, lock.o nlm_prot_clnt.o nlm_prot_xdr.o pmap_prot_clnt.o pmap_prot_xdr.o util.o rpc.o parson.o) | bin
	gcc ${CFLAGS} $^ -o $@

tests: tests/util_tests
tests/util_tests: tests/util_tests.c tests/minunit.h src/util.o src/util.h
	gcc ${CFLAGS} $^ -o $@
	tests/util_tests

# man pages
man: $(addprefix man/man8/, nfsping.8 nfsdf.8 nfsls.8 nfsmount.8 nfslock.8 nfscat.8)

# quick install
install: $(addprefix $(prefix)/bin/, $(all)) $(addsuffix .8, $(addprefix $(prefix)/share/man/man8/, $(all)))

# include generated dependency files
ifneq ($(MAKECMDGOALS),clean)
-include $(SRC:src/%.c=deps/%.d)
endif
