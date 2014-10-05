/* generic RPC functions */

#include "nfsping.h"

extern int verbose;


/* look up a remote RPC program's port using the portmapper */
/* this replaces pmap_getport but lets us use our own RPC client connection */
/* pmap_getport uses its own client so you can't specify a source IP address for example */
/* return the port in network byte order */
/* protocol can be PMAP_IPPROTO_TCP or PMAP_IPPROTO_UDP */
uint16_t get_rpc_port(CLIENT *client, long unsigned prognum, long unsigned version, long unsigned protocol) {
    u_long *port;
    pmap pmap_args = {
        .pm_prog = prognum,
        .pm_vers = version,
        .pm_prot = protocol,
    };

    port = pmapproc_getport_2(&pmap_args, client);

    /* 0 is failure */
    if (port == 0) {
        clnt_perror(client, "pmapproc_getport_2");
        exit(EXIT_FAILURE);
    }

    return(htons(*port));
}


/* destroy an RPC client */
CLIENT *destroy_rpc_client(CLIENT *client) {
    if (client) {
        /* have to clean this up first */
        auth_destroy(client->cl_auth);
        /* this should close the socket */
        clnt_destroy(client);
    }

    return NULL;
}


/* create an RPC client */
/* takes an initialised sockaddr_in with the address and port */
CLIENT *create_rpc_client(struct sockaddr_in *client_sock, struct addrinfo *hints, unsigned long prognum, unsigned long version, struct timeval timeout, struct sockaddr_in src_ip) {
    CLIENT *client;
    int sock;
    long unsigned protocol; /* for portmapper */
    char ip[INET_ADDRSTRLEN];
    struct sockaddr_in getaddr; /* for getsockname */
    socklen_t len = sizeof(getaddr);

    /* Even if you specify a source address the portmapper will use the default one */
    /* this applies to pmap_getport or clnt*_create */
    /* so use our own get_rpc_port */

    /* check if we need to use the portmapper */
    if (client_sock->sin_port == 0) {
        client_sock->sin_port = htons(PMAPPORT); /* 111 */

        sock = socket(AF_INET, hints->ai_socktype, 0);
        if (sock < 0) {
            perror("create_rpc_client");
            exit(EXIT_FAILURE); /* TODO should this be a different return code? */
        }

        /* set the source address if specified */
        if (src_ip.sin_addr.s_addr) {
            /* portmapper doesn't need a reserved port */
            src_ip.sin_port = 0;

            if (bind(sock, (struct sockaddr *) &src_ip, sizeof(src_ip)) == -1) {
                perror("create_rpc_client");
                exit(EXIT_FAILURE); /* TODO should this be a different return code? */
            }
        }

        if (connect(sock, (struct sockaddr *)client_sock, sizeof(struct sockaddr)) == 0) {
            /* TCP */
            if (hints->ai_socktype == SOCK_STREAM) {
                protocol = PMAP_IPPROTO_TCP;
                    client = clnttcp_create(client_sock, PMAPPROG, PMAPVERS, &sock, 0, 0);
                    if (client == NULL) {
                        clnt_pcreateerror("clnttcp_create");
                    }
            /* UDP */
            } else {
                protocol = PMAP_IPPROTO_UDP;
                client = clntudp_create(client_sock, PMAPPROG, PMAPVERS, timeout, &sock);
                if (client == NULL) {
                    clnt_pcreateerror("clntudp_create");
                }
            }
        } else {
            perror("connect");
            exit(EXIT_FAILURE);
        }

        if (verbose) {
            if (getsockname(sock, (struct sockaddr *)&getaddr, &len) == -1) {
                perror("getsockname");
            } else {
                inet_ntop(AF_INET, (struct sockaddr_in *)&getaddr.sin_addr, ip, INET_ADDRSTRLEN);
                debug("portmap source port = %s:%u\n", ip, ntohs(getaddr.sin_port));
            }
        }

        /* query the portmapper */
        client_sock->sin_port = get_rpc_port(client, prognum, version, protocol);

        /* close the portmapper connection */
        client = destroy_rpc_client(client);

        /* by this point we should know which port we're talking to */
        debug("portmapper = %u\n", ntohs(client_sock->sin_port));
    }

    /* now make the client connection */

    /* Make sure and make new sockets for each new connection */
    /* clnttcp_create will happily reuse open sockets */
    sock = socket(AF_INET, hints->ai_socktype, 0);
    if (sock < 0) {
        perror("create_rpc_client(socket)");
        exit(EXIT_FAILURE); /* TODO should this be a different return code? */
    }

    /* always try and bind to a low port first */
    /* could check for root here but there are other mechanisms for allowing processes to bind to low ports */

    /* try a reserved port first and see what happens */
    /* start in the middle of the range so we're away from really low ports like 22 and 80 */
    src_ip.sin_port = htons(666);

    while (bind(sock, (struct sockaddr *) &src_ip, sizeof(struct sockaddr)) == -1) {
        /* permission denied, ie we aren't root */
        if (errno == EACCES) {
            /* try an ephemeral port */
            src_ip.sin_port = htons(0);
        /* blocked, try the next port */
        } else if (errno == EADDRINUSE) {
            /* TODO should we keep track of how many times we're looping through low ports and complain or give up? */
            if (ntohs(src_ip.sin_port) < 1023) {
                src_ip.sin_port = htons(ntohs(src_ip.sin_port) + 1);
            /* start over */
            } else {
                src_ip.sin_port = htons(1);
            }
        } else {
            perror("create_rpc_client(bind)");
            exit(EXIT_FAILURE); /* TODO should this be a different return code? */
        }
    }

    /* now we're bound to a local socket, try and connect to the server */
    if (connect(sock, (struct sockaddr *)client_sock, sizeof(struct sockaddr)) == 0) {
        /* TCP */
        if (hints->ai_socktype == SOCK_STREAM) {
                /* TODO set recvsz and sendsz to the NFS blocksize */
                client = clnttcp_create(client_sock, prognum, version, &sock, 0, 0);
                if (client == NULL) {
                    clnt_pcreateerror("clnttcp_create");
                }
        /* UDP */
        } else {
            client = clntudp_create(client_sock, prognum, version, timeout, &sock);
            if (client == NULL) {
                clnt_pcreateerror("clntudp_create");
            }
        }
    } else {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    if (verbose) {
        if (getsockname(sock, (struct sockaddr *)&getaddr, &len) == -1) {
            perror("getsockname");
        } else {
            inet_ntop(AF_INET, (struct sockaddr_in *)&getaddr.sin_addr, ip, INET_ADDRSTRLEN);
            debug("source port = %s:%u\n", ip, ntohs(getaddr.sin_port));
        }
    }

    if (client) {
        client->cl_auth = authnone_create();
        clnt_control(client, CLSET_TIMEOUT, (char *)&timeout);
    }

    return client;
}
