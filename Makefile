.PHONY: all clean rpcgen nfsping nfsmount nfsdf nfscat

all: nfsping nfsmount nfsdf nfsls nfscat

clean:
	rm -rf obj bin deps rpcsrc/*.c rpcsrc/*.h

#output directories
bin obj deps:
	mkdir $@

CFLAGS = -Werror -g -I src -I rpcsrc
# generate header dependencies
CPPFLAGS += -MMD -MP

# phony target to generate rpc files
# we're only really interested in the generated headers so gcc can figure out the rest of the dependencies
rpcgen: rpcsrc/nfs_prot.h rpcsrc/mount.h

# change to the rpcsrc directory first so output files go in the same directory

#rpcgen NFS
$(addprefix rpcsrc/, nfs_prot.h nfs_prot_clnt.c nfs_prot_svc.c nfs_prot_xdr.c): rpcsrc/nfs_prot.x
	cd rpcsrc && rpcgen -DWANT_NFS3 nfs_prot.x

#rpcgen MOUNT
$(addprefix rpcsrc/, mount.h mount_clnt.c mount_svc.c mount_xdr.c): rpcsrc/mount.x
	cd rpcsrc && rpcgen -DWANT_NFS3 mount.x

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

# make the bin directory first if it's not already there

nfsping: bin/nfsping
bin/nfsping: $(addprefix obj/, nfsping.o nfs_prot_clnt.o nfs_prot_xdr.o mount_clnt.o mount_xdr.o util.o rpc.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfsmount: bin/nfsmount
bin/nfsmount: $(addprefix obj/, mount.o mount_clnt.o mount_xdr.o rpc.o util.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfsdf: bin/nfsdf
bin/nfsdf: $(addprefix obj/, df.o nfs_prot_clnt.o nfs_prot_xdr.o util.o rpc.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfsls: bin/nfsls
bin/nfsls: $(addprefix obj/, ls.o nfs_prot_clnt.o nfs_prot_xdr.o util.o rpc.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfscat: bin/nfscat
bin/nfscat: $(addprefix obj/, cat.o nfs_prot_clnt.o nfs_prot_xdr.o util.o rpc.o) | bin
	gcc ${CFLAGS} $^ -o $@

tests: tests/util_tests
tests/util_tests: tests/util_tests.c tests/minunit.h util.o util.h
	gcc ${CFLAGS} $^ -o $@

# include generated dependency files
ifneq ($(MAKECMDGOALS),clean)
-include $(SRC:src/%.c=deps/%.d)
endif
