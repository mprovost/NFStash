all: nfsping

nfsping: src/nfsping.c src/nfsping.h src/nfs_prot_clnt.c src/mount_clnt.c src/util.c
	gcc src/nfsping.c src/nfs_prot_clnt.c src/nfs_prot_xdr.c src/mount_clnt.c src/mount_xdr.c src/util.c -g -o $@

nfs_prot.h nfs_prot_clnt.c nfs_prot_svc.c nfs_prot_xdr.c: nfs_prot.x
	rpcgen -DWANT_NFS3 $<

mount.h mount_clnt.c mount_svc.c mount_xdr.c: mount.x
	rpcgen -DWANT_NFS3 $<

nfsmount: mount.c nfsping.h
	gcc mount.c mount_clnt.c mount_xdr.c -g -o $@

nfsdf: df.c nfsping.h
	gcc df.c nfs_prot_clnt.c nfs_prot_xdr.c -g -o $@

tests: tests/util_tests

tests/util_tests: tests/util_tests.c tests/minunit.h src/util.c
	gcc -I src tests/util_tests.c src/util.c -g -o $@
