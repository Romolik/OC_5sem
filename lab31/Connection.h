#include "HTTPParser.h"

void add_connection(int clientSocket, PollFDs& poll_fds, Connections& connections);

int set_client_cache(ConnectionDescriptor* connection, short& events, const Cache& cache);

void set_client_nocache(ConnectionDescriptor* connection, short& events);

void set_server_cache(ConnectionDescriptor* connection, short& events);

void set_server_nocache(ConnectionDescriptor* connection, short& events);

int try_to_parse_headers(ConnectionDescriptor* connection, Cache& cache, PollFDs& poll_fds, Connections& connections);

int init_connection_with_server(const char *host, Pair &cacheKey, ConnectionDescriptor *connection, PollFDs& poll_fds,
								Connections& connections);

void drop_connection(ConnectionDescriptor* connection, long unsigned int* position, Cache& cache, PollFDs& poll_fds,
					 Connections& connections);

std::string string_cache_key(const Pair *key);

void check_request(ConnectionDescriptor *connection);