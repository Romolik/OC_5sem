#include "HTTPParser.h"

std::string string_cache_key(const Pair *key);

void check_request(ConnectionDescriptor *connection);

void add_connection(Connections &connections, PollFDs &poll_fds, int clientSocket);

void set_client_cache(ConnectionDescriptor *connection, short &events, SharedData& sharedData);

void set_client_nocache(ConnectionDescriptor *connection, short &events);

void set_server_cache(ConnectionDescriptor *connection, short &events);

void set_server_nocache(ConnectionDescriptor *connection, short &events);

int try_to_parse_headers(Connections &connections, PollFDs &poll_fds,
						 ConnectionDescriptor *connection, pthread_t threads[],
						 SharedData& sharedData);

void drop_connection(Connections &connections, PollFDs &poll_fds, ConnectionDescriptor *connection, int *position,
					 SharedData& sharedData);

int init_connection_with_server(const char *host, Pair &cacheKey, ConnectionDescriptor *connection, SharedData& sharedData);