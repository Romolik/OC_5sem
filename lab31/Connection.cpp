#include "Connection.h"

#include <netdb.h>
#include <fcntl.h>
#include <strings.h>
#include <poll.h>
#include <cassert>

int set_client_cache(ConnectionDescriptor *connection, short &events, const Cache& cache) {
	assert(nullptr != connection);
	assert(connection->type == CLIENT);

	auto &&cacheNode = cache.find(*connection->cacheKey);
	if (cacheNode == cache.end()) {
		if (cacheNode == cache.end()) {
			fprintf(stderr, "set_client_cache(): not found in cache\n");
			return FAILURE;
		}
	}
	check_request(connection);
	if (connection->fullRequest) {// has already received a full request from the client
		if (connection->currentBlock == nullptr) {
			if (cacheNode->second.data == nullptr) {
				return SUCCESS;
			}
			connection->currentBlock = cacheNode->second.data;
		}
		if (connection->currentBlock != cacheNode->second.endOfData) {
			// some data from the server to write to the client
			events |= POLLOUT;
		} else { // last data block, the server closed the connection
			if (!cacheNode->second.downloading) {
				events |= POLLOUT;
			}
		}
	} else {
		fprintf(stderr, "set_client_cache(): not full request\n");
	}
	return SUCCESS;
}

void set_server_cache(ConnectionDescriptor *connection, short &events) {
	assert(nullptr != connection);

	if (!connection->connected) {
		fprintf(stderr, "set_server_cache(): not connected\n");
		events |= POLLOUT;
		return;
	} else {
		events |= POLLIN;
	}
	check_request(connection);
	if (!connection->fullRequest) { // has not sent a full request yet
		if (connection->bsize > 0) { // has some data to send
			events |= POLLOUT;
			return;
		}
// look how many bytes are left
		ssize_t interval = connection->worker->requestLength - connection->requestLength;
		assert(connection->worker != nullptr);
		if (interval < 0) {
			fprintf(stderr, "worker : [%d]\n", connection->worker->socket);
			fprintf(stderr, "worker request length == %ld\n", connection->worker->requestLength);
			fprintf(stderr, "request length == %ld\n", connection->requestLength);
		} else {
			fprintf(stderr, "worker : [%d]\n", connection->worker->socket);
		}
		assert(interval >= 0);
// update the request pointer, if the memory was reallocated
		connection->request = connection->worker->request;
		ssize_t len = ((interval) > BUFFER_SIZE) ? BUFFER_SIZE : interval;
// copy a part of the request to the buffer for sending
		memcpy(connection->buffer, &connection->request[connection->requestLength], len);
		connection->requestLength += len;
		connection->bsize = len;
		if (connection->bsize > 0) { // has some data to send
			events |= POLLOUT;
		}
	}
}

std::string string_cache_key(const Pair *key) {
	if (key == nullptr) {
		return "{nullptr}";
	}
	try {
		return "{Host: " + key->first + ", URI: " + key->second + "}";
	} catch (std::exception &ex) {
		fprintf(stderr, "caught exception: %s\n", ex.what());
		return "{error}";
	}
}

void set_client_nocache(ConnectionDescriptor *connection, short &events) {
	assert(nullptr != connection);

	if (connection->fullRequest) { // has already received a full request from the client
		if(connection->worker != nullptr) {
			if (connection->worker->fullRequest) { // has already sent a full request to the server
				if (connection->bsize == 0 && connection->worker->bsize > 0) {
// transfer data from server buffer to client buffer
					bzero(connection->buffer, sizeof(connection->buffer));
					memcpy(connection->buffer, connection->worker->buffer, connection->worker->bsize);
					connection->bsize = connection->worker->bsize;
					connection->worker->bsize = 0;
				}
				if (connection->bsize > 0) {
// some data to write to the client
					events |= POLLOUT;
				}
			}
		}
	}
}

void set_server_nocache(ConnectionDescriptor *connection, short &events) {
	assert(nullptr != connection);

	if (!connection->connected) {
		events |= POLLOUT;
		return;
	} else {
		events |= POLLIN;
	}
	if (!connection->fullRequest) { // has not sent a full request yet
		// transfer data from client buffer to server buffer
		if (connection->bsize == 0 && connection->worker->bsize > 0) {
			bzero(connection->buffer, sizeof(connection->buffer));
			memcpy(connection->buffer, connection->worker->buffer, connection->worker->bsize);
			connection->bsize = connection->worker->bsize;
			connection->worker->bsize = 0;
		}
	}
	if (connection->bsize > 0) { // has some data to send
		events = 0;
		events |= POLLOUT;
	}
}

void drop_connection(ConnectionDescriptor *connection, long unsigned int* pos, Cache& cache, PollFDs& poll_fds
	, Connections& connections) {
	assert(nullptr != connection);
	assert(nullptr != pos);

	int sock = connection->socket;
	std::string type = (connection->type == CLIENT) ? "client" : "server";
	fprintf(stderr, "drop_connection(): init %s [%d]\n", type.c_str(), sock);
	if (connection->type == CLIENT) {
		if (connection->cacheKey != nullptr) {
			fprintf(stderr, "found cacheNode in cache\n");
			auto &&cacheNode = cache.find(*connection->cacheKey);
			assert(cacheNode != cache.end());
			cacheNode->second.clientCounter--;
			if (cacheNode->second.clientCounter == 0 && connection->useCache == true) {
				if (cacheNode->second.downloading ||// remove if there's no other client on this page
					cacheNode->second.error) { //and it hasn't downloaded yet or an error has occured
					fprintf(stderr, "removed from cache\n");
					auto data = cacheNode->second.data;
					while (data != nullptr) {
						auto next = data->nextBlock;
						delete (data);
						data = next;
					}
					cache.erase(cacheNode->first);
				}
			} else if (cacheNode->second.clientCounter == 0 && connection->useCache == false) {
				fprintf(stderr, "removed from nocache\n");
				cache.erase(cacheNode->first);
			}
		}
	} else {
		auto &&cacheNode = cache.find(*connection->cacheKey);
		if (cacheNode != cache.end()) {
			if (connection->error) {
				cacheNode->second.error = SERVER_ERR;
			}
			cacheNode->second.downloading = false;
		}
	}

	switch (connection->error) {
		case UNSUPPORTED_METH:
			write(connection->socket, UNSUPPORTED_METHOD, sizeof(UNSUPPORTED_METHOD));
			break;
		case SERVER_ERR:
			write(connection->socket, SERVER_ERROR, sizeof(SERVER_ERROR));
			break;
		case WRONG_VER:
			write(connection->socket, WRONG_VERSION, sizeof(WRONG_VERSION));
			break;
		case WRONG_REQ:
			write(connection->socket, WRONG_REQUEST, sizeof(WRONG_REQUEST));
			break;
		default:
			perror("connection error");
	}
	if (connection->worker != nullptr) {
		connection->worker->worker = nullptr;
	}
	poll_fds.erase(poll_fds.begin() + *pos);
	(*pos)--;
	connections.erase(connection->socket);
	fprintf(stderr, "drop_connection(): end %s [%d]\n", type.c_str(), sock);
	delete (connection);
}

void add_connection(int clientSocket, PollFDs& poll_fds, Connections& connections) {
	assert(clientSocket > 0);

	ConnectionDescriptor *connection;
	try {
		connection = new ConnectionDescriptor;
	} catch (const std::bad_alloc &e) {
		std::cout << e.what() << '\n';
		throw;
	}
	connection->type = CLIENT;
	fcntl(clientSocket, F_SETFL, O_NONBLOCK);
	connection->socket = clientSocket;
	poll_fds.push_back({clientSocket, POLLIN, 0});
	connections[clientSocket] = connection;
}

int init_connection_with_server(const char *host, Pair &cacheKey, ConnectionDescriptor *connection, PollFDs& poll_fds,
								Connections& connections) {
	assert(nullptr != host);
	assert(nullptr != connection);

	struct hostent *hp;
	int errnum;
	hp = getipnodebyname(host, AF_INET, AI_DEFAULT, &errnum);
	if (hp == nullptr) {
		fprintf(stderr, "init_connection_with_server(): ");
		switch (errnum) {
			case HOST_NOT_FOUND:
				fprintf(stderr, "Host is unknown.\n");
				connection->error = WRONG_REQ;
				break;
			case NO_DATA:
				fprintf(stderr, "No address is available for the name specified in the server request.\n");
				connection->error = WRONG_REQ;
				break;
			case NO_RECOVERY:
				fprintf(stderr, "An unexpected server failure occurred, which is a non-recoverable error.\n");
				break;
			case TRY_AGAIN:
				fprintf(stderr, "Try again later.\n");
				break;
			default:
				fprintf(stderr, "Unknown error.\n");
				break;
		}
		return SERVER_ERR;
	}
	struct sockaddr_in serverAddr;
	bzero(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	memcpy(&serverAddr.sin_addr.s_addr, hp->h_addr_list[0], sizeof(serverAddr.sin_addr.s_addr));
	serverAddr.sin_port = htons(80);
	freehostent(hp);
	int serverSocket;
	if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("init_connection_with_server(): socket() error");
		return SERVER_ERR;
	}
	fcntl(serverSocket, F_SETFL, O_NONBLOCK);
	if (connect(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
		if (errno != EINPROGRESS && errno != EALREADY) {
			perror("init_connection_with_server(): connect() error");
			return SERVER_ERR;
		}
	}
	ConnectionDescriptor* serverConnection;
	try {
		serverConnection = new ConnectionDescriptor;
		serverConnection->socket = serverSocket;
		serverConnection->cacheKey = new Pair({cacheKey.first, cacheKey.second});
	} catch (const std::bad_alloc &e) {
		std::cout << e.what() << '\n';
		throw;
	}
	fprintf(stderr, "init_connection_with_server(): server cacheKey: %s\n",
			string_cache_key(serverConnection->cacheKey).c_str());
	serverConnection->worker = connection;
	serverConnection->useCache = serverConnection->worker->useCache;
	serverConnection->type = SERVER;
	serverConnection->connected = false;
	serverConnection->status = CONNECTED;
	connection->worker = serverConnection;
	poll_fds.push_back({serverSocket, POLLIN, 0});
	connections[serverSocket] = serverConnection;
	return SUCCESS;
}

int try_to_parse_headers(ConnectionDescriptor *connection, Cache& cache, PollFDs& poll_fds, Connections& connections) {
	std::string headers;
	if (connection->request == NULL) {
		fprintf(stderr, "\nrequest is nullptr\n");
		return NOT_READY;
	}
	headers.assign(connection->request);
	long unsigned int pos;
	pos = headers.find("GET", 0, connection->bsize);
	const int lenghtGet = 3;
	if (pos == std::string::npos) { //not found, method != GET
		if (connection->bsize > lenghtGet) {
			fprintf(stderr, "\ntry_to_parse_headers(): Unsupported method\n");
			connection->error = UNSUPPORTED_METH;
			return DROP;
		}
		fprintf(stderr, "\nNot found method \"GET\"\n");
		return NOT_READY;
	} else {
		pos += (lenghtGet + 1);
		const long unsigned int end = headers.find_first_of(' ', pos);
		if (end != std::string::npos) {
			Pair resource;
			std::string uri = headers.substr(pos, end - pos);
			std::string host = find_host(headers);
			if (host.empty()) {
				fprintf(stderr, "\nHost is empty\n");
				return NOT_READY;
			} else {
				fprintf(stderr, "\nHost == %s\n", host.c_str());
			}
			if (check_protocol(headers, connection->request) != SUCCESS) {
				fprintf(stderr, "\ntry_to_parse_headers(): Wrong protocol\n");
				connection->error = WRONG_VER;
				return DROP;
			}
			resource = {host, uri};
			Cache::iterator iter;
			if ((iter = cache.find(resource)) == cache.end()) { //no such record in cache
				PageDescriptor page;
				page.clientCounter++;
				auto result = cache.insert({resource, page});
				fprintf(stderr, "\ntry_to_parse_headers(): try to init connection with server\n");
				if (!result.second
					|| init_connection_with_server(host.c_str(), resource, connection, poll_fds, connections)
						!= SUCCESS) {
					fprintf(stderr, "\ntry_to_parse_headers(): Cannot init connection with server\n");
					cache.erase(resource);
					if (!connection->error) {
						connection->error = SERVER_ERR;
					}
					return DROP;
				} else {
					connection->cacheKey = new Pair(resource);
				}
			} else {
				fprintf(stderr, "\ntry_to_parse_headers(): already in cache\n");
				(*iter).second.clientCounter++;
				connection->cacheKey = new Pair(resource);
			}
//                      else {
//                              if (iter->second.useCache == true) {
//                                      fprintf(stderr, "\ntry_to_parse_headers(): already in cache\n");
//                                      (*iter).second.clientCounter++;
//                                      connection->cacheKey = new Pair(resource);
//                              } else {
//                                      //connection->useCache = false;
//                                      fprintf(stderr, "\ntry_to_parse_headers(): already in nocache\n");
//                                      if (init_connection_with_server(host.c_str(), resource, connection, poll_fds, connections)
//                                                      != SUCCESS) {
//                                              fprintf(stderr, "\ntry_to_parse_headers(): Cannot init connection with server\n");
//                                              if (!connection->error) {
//                                                      connection->error = SERVER_ERR;
//                                              }
//                                              return DROP;
//                                      } else {
//                                              connection->cacheKey = new Pair(resource);
//                                      }
//                              }
//                      }
			if (headers.find("\r\n\r\n") != std::string::npos) {
				connection->fullRequest = true;
			}
			fprintf(stderr, "Got request:\n%s", headers.c_str());
			fprintf(stderr, "Host == %s\n", host.c_str());
			fprintf(stderr, "URI == %s\n", uri.c_str());
			return SUCCESS;
		} else {
			fprintf(stderr, "\n" R"(not found "\r\n ")"  "\n");
			return NOT_READY;
		}
	}
}

void check_request(ConnectionDescriptor *connection) {
	assert(nullptr != connection);

	if (!connection->fullRequest) {
		std::string req;
		req.assign(connection->request, connection->requestLength);
		if (req.find("\r\n\r\n") != std::string::npos) {
			connection->fullRequest = true;
		}
		if (req.find("\n\n") != std::string::npos) {
			connection->fullRequest = true;
		}
	}
}
