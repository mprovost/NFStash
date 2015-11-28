.PHONY: all clean rpcgen nfsping nfsmount nfsdf nfscat man install

all = nfsping nfsmount nfsdf nfsls nfscat nfslock
all: $(all) man

# installation directory
prefix = /usr/local

clean:
	rm -rf obj bin deps man rpcsrc/*.c rpcsrc/*.h config/*.opt

#output directories
bin obj deps man/man8:
	mkdir -p $@

CFLAGS = -Werror -g -I src -I.
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

# config - check for clock_gettime in libc or librt
# this opt file gets included as a gcc option
config/clock_gettime.opt:
	cd config && ./clock_gettime.sh

# common object files
common_objs = $(addsuffix .o, pmap_prot_clnt pmap_prot_xdr util rpc parson)

# make the bin directory first if it's not already there
nfsping: bin/nfsping
nfsping_objs = $(addprefix obj/, $(addsuffix .o, nfsping nfs_prot_clnt nfs_prot_xdr nfsv4_prot_clnt nfsv4_prot_xdr mount_clnt mount_xdr nlm_prot_clnt nlm_prot_xdr nfs_acl_clnt sm_inter_clnt sm_inter_xdr rquota_clnt rquota_xdr) $(common_objs))
bin/nfsping: config/clock_gettime.opt $(nfsping_objs) | bin
	gcc ${CFLAGS} @config/clock_gettime.opt $(nfsping_objs) -o $@

# TODO addsuffix .o
nfsmount: bin/nfsmount
bin/nfsmount: $(addprefix obj/, mount.o mount_clnt.o mount_xdr.o pmap_prot_clnt.o $(common_objs)) | bin
	gcc ${CFLAGS} $^ -o $@

nfsdf: bin/nfsdf
bin/nfsdf: $(addprefix obj/, df.o nfs_prot_clnt.o nfs_prot_xdr.o $(common_objs)) | bin
	gcc ${CFLAGS} $^ -o $@

nfsls: bin/nfsls
bin/nfsls: $(addprefix obj/, ls.o nfs_prot_clnt.o nfs_prot_xdr.o $(common_objs)) | bin
	gcc ${CFLAGS} $^ -o $@

nfscat: bin/nfscat
bin/nfscat: $(addprefix obj/, cat.o nfs_prot_clnt.o nfs_prot_xdr.o $(common_objs)) | bin
	gcc ${CFLAGS} $^ -o $@

nfslock: bin/nfslock
nfslock_objs = $(addprefix obj/, $(addsuffix .o, lock nlm_prot_clnt nlm_prot_xdr) $(common_objs))
bin/nfslock: config/clock_gettime.opt $(nfslock_objs) | bin
	gcc ${CFLAGS} @config/clock_gettime.opt $(nfslock_objs) -o $@

tests: tests/util_tests
tests/util_tests: tests/util_tests.c tests/minunit.h src/util.o obj/parson.o src/util.h | rpcgen
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
