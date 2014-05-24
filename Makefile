all: nfsping

clean:
	rm -rf obj bin

#output directories
bin:
	mkdir $@

obj:
	mkdir $@

CFLAGS=-Werror -g -I src -I.

# build RPC headers first
src/nfsping.h: src/nfs_prot.h src/mount.h

src/nfs_prot.h src/nfs_prot_clnt.c src/nfs_prot_svc.c src/nfs_prot_xdr.c: src/nfs_prot.x
	rpcgen -DWANT_NFS3 $<

src/mount.h src/mount_clnt.c src/mount_svc.c src/mount_xdr.c: src/mount.x
	rpcgen -DWANT_NFS3 $<

#objects
obj/%.o: src/%.c | obj
	gcc ${CFLAGS} -c -o $@ $<

nfsping: $(addprefix obj/, nfsping.o nfs_prot_clnt.o nfs_prot_xdr.o mount_clnt.o mount_xdr.o util.o rpc.o) src/nfsping.h 
	gcc $^ -o $@

#nfsping: src/nfsping.c src/nfsping.h src/nfs_prot_clnt.c src/mount_clnt.c src/util.c src/rpc.c
	#gcc ${CFLAGS} src/nfsping.c src/nfs_prot_clnt.c src/nfs_prot_xdr.c src/mount_clnt.c src/mount_xdr.c src/util.c src/rpc.c -o $@

nfsmount: src/mount.c src/nfsping.h src/mount_clnt.c src/mount_xdr.c src/rpc.c src/util.c src/util.h
	gcc ${CFLAGS} $^ -o $@

nfsdf: src/df.c src/nfsping.h src/nfs_prot_clnt.c src/nfs_prot_xdr.c src/util.c src/rpc.c
	gcc ${CFLAGS} $^ -o $@

nfsls: src/ls.c src/nfs_prot_clnt.c src/nfs_prot_xdr.c src/util.c src/rpc.c
	gcc ${CFLAGS} $^ -o $@

nfscat: src/cat.c src/nfs_prot_clnt.c src/nfs_prot_xdr.c src/util.c src/rpc.c
	gcc ${CFLAGS} $^ -o $@

tests: tests/util_tests

tests/util_tests: tests/util_tests.c tests/minunit.h src/util.c src/util.h
	gcc ${CFLAGS} tests/util_tests.c src/util.c -o $@
