CLIENT *create_rpc_client(struct sockaddr_in *client_sock, struct addrinfo *hints, uint16_t port, unsigned long prognum, unsigned long version, struct timeval timeout);
void destroy_rpc_client(CLIENT *client);
