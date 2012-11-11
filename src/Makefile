nfsping: nfsping.c nfsping.h nfs_prot_clnt.c mount_clnt.c
	gcc nfsping.c nfs_prot_clnt.c nfs_prot_xdr.c mount_clnt.c mount_xdr.c -g -o $@

nfs_prot.h nfs_prot_clnt.c nfs_prot_svc.c nfs_prot_xdr.c: nfs_prot.x
	rpcgen -DWANT_NFS3 $<

mount.h mount_clnt.c mount_svc.c mount_xdr.c: mount.x
	rpcgen -DWANT_NFS3 $<

nfsmount: mount.c nfsping.h
	gcc mount.c mount_clnt.c mount_xdr.c -g -o $@

nfsdf: df.c nfsping.h
	gcc df.c nfs_prot_clnt.c nfs_prot_xdr.c -g -o $@
