#!/bin/sh

echo "Finding svc_fdset in libc:"
if `gcc rpc.c`
then
    echo "ok"
    > rpc.cflags
    > rpc.ldflags
elif `gcc -I /usr/include/tirpc rpc.c`
then
    echo "Finding svc_fdset in libtirpc:"
    echo "ok"
    echo "-I /usr/include/tirpc -D_RPC_PMAP_PROT_H=1" > rpc.cflags
    echo "-ltirpc" > rpc.ldflags
else
    echo "Couldn't find svc_fdset!"
    exit 1
fi

# remove the dummy output
rm a.out
