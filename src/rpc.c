/* generic RPC functions */

#include "nfsping.h"

/* create an RPC client */
/* takes an initialised sockaddr_in with the address and port */
CLIENT *create_rpc_client(struct sockaddr_in *client_sock, struct addrinfo *hints, unsigned long prognum, unsigned long version, struct timeval timeout, struct sockaddr_in src_ip) {
    CLIENT *client;
    int sock;

    /* Make sure and make new sockets for each new connection */
    /* clnttcp_create will happily reuse open sockets */

    /* Even if you specify a source address the portmapper will use the default one */
    /* this applies to pmap_getport or clnt*_create */

    /* set the source address if specified */
    if (src_ip.sin_addr.s_addr) {
        sock = socket(AF_INET, hints->ai_socktype, 0);
        if (sock < 0) {
            perror("create_rpc_client");
            exit(EXIT_FAILURE); /* TODO should this be a different return code? */
        }

        /* could check for root here but there are other mechanisms for allowing processes to bind to low ports */

        /* try a reserved port first and see what happens */
        /* start in the middle of the range so we're away from really low ports like 22 and 80 */
        src_ip.sin_port = htons(666);

        while (ntohs(src_ip.sin_port) < 1024) {
            if (bind(sock, (struct sockaddr *) &src_ip, sizeof(src_ip)) < 0) {
                /* permission denied, ie we aren't root */
                if (errno == EACCES) {
                    /* try an ephemeral port */
                    src_ip.sin_port = 0;
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
                    perror("create_rpc_client");
                    exit(EXIT_FAILURE); /* TODO should this be a different return code? */
                }
            /* it worked, we have a socket */
            } else {
                break;
            }
        }
    /* use any address/port */
    } else {
        sock = RPC_ANYSOCK;
    }

    /* TCP */
    if (hints->ai_socktype == SOCK_STREAM) {
        /* check the portmapper */
        /* this makes a separate connection, lame */
        if (client_sock->sin_port == 0)
            client_sock->sin_port = htons(pmap_getport(client_sock, prognum, version, IPPROTO_TCP));
        /* TODO set recvsz and sendsz to the NFS blocksize */
        client = clnttcp_create(client_sock, prognum, version, &sock, 0, 0);

        if (client == NULL) {
            clnt_pcreateerror("clnttcp_create");
        }
    /* UDP */
    } else {
        /* check the portmapper */
        /* this makes a separate connection, lame */
        if (client_sock->sin_port == 0)
            client_sock->sin_port = htons(pmap_getport(client_sock, prognum, version, IPPROTO_UDP));
        client_sock->sin_port == 0;
        client = clntudp_create(client_sock, prognum, version, timeout, &sock);

        if (client == NULL) {
            clnt_pcreateerror("clntudp_create");
        }
    }

    /* check if the portmapper failed */
    /* by this point we should know which port we're talking to */
    if (client_sock->sin_port == 0) {
        clnt_pcreateerror("pmap_getport");
    }

    if (client) {
        client->cl_auth = authnone_create();
        clnt_control(client, CLSET_TIMEOUT, (char *)&timeout);
    }

    return client;
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
