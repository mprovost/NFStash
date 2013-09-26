/* generic RPC functions */

#include "nfsping.h"

/* create an RPC client */
CLIENT *create_rpc_client(struct sockaddr_in *client_sock, struct addrinfo *hints, uint16_t port, unsigned long prognum, unsigned long version, struct timeval timeout) {
    CLIENT *client;

    /* make sure and set this for each new connection so it gets a new socket */
    /* clnttcp_create will happily reuse sockets */
    int sock = RPC_ANYSOCK;

    client_sock->sin_family = AF_INET;

    if (port)
        client_sock->sin_port = port;

    /* TCP */
    if (hints->ai_socktype == SOCK_STREAM) {
        /* check the portmapper */
        if (port == 0)
            client_sock->sin_port = htons(pmap_getport(client_sock, prognum, version, IPPROTO_TCP));
        /* TODO set recvsz and sendsz to the NFS blocksize */
        client = clnttcp_create(client_sock, prognum, version, &sock, 0, 0);

        if (client == NULL) {
            clnt_pcreateerror("clnttcp_create");
        }
    /* UDP */
    } else {
        /* check the portmapper */
        if (port == 0)
            client_sock->sin_port = htons(pmap_getport(client_sock, prognum, version, IPPROTO_UDP));
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
