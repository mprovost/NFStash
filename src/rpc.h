#ifndef RPC_H
#define RPC_H

CLIENT *create_rpc_client(struct sockaddr_in *client_sock, struct addrinfo *hints, unsigned long prognum, unsigned long version, struct timeval timeout, struct sockaddr_in src_ip);
CLIENT *destroy_rpc_client(CLIENT *client);

#endif /* RPC_H */
