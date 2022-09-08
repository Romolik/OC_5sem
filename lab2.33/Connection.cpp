#include "Connection.h"

#include <netdb.h>
#include <fcntl.h>
#include <strings.h>
#include <poll.h>
#include <cassert>

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

void check_request(ConnectionDescriptor *connection) {
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

void add_connection(Connections &connections, PollFDs &poll_fds, int clientSocket) {
	auto *connection = new ConnectionDescriptor;
	connection->type = CLIENT;
	fcntl(clientSocket, F_SETFL, O_NONBLOCK);
	connection->socket = clientSocket;
	poll_fds.push_back({clientSocket, POLLIN, 0});
	connections[clientSocket] = connection;
}


void set_client_cache(ConnectionDescriptor *connection, short &events, SharedData& sharedData) {
	assert(connection->type == CLIENT);
	pthread_t thread = pthread_self();
	sharedData.cacheSync.read_lock();
	auto &&cacheNode = sharedData.cache.find(*connection->cacheKey);
	if (cacheNode == sharedData.cache.end()) {
		if (cacheNode == sharedData.cache.end()) {
			sharedData.cacheSync.unlock();
			fprintf(stderr, "[%d] set_client_cache(): not found in cache\n", thread);
			exit(6);
		}
	}
	check_request(connection);
	if (connection->fullRequest) {// has already received a full request from the client
		cacheNode->second.sync.read_lock();
		if (connection->currentBlock == nullptr) {
			if (cacheNode->second.data == nullptr) {
				cacheNode->second.sync.unlock();
				sharedData.cacheSync.unlock();
				return;
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
		cacheNode->second.sync.unlock();
	} else {
		fprintf(stderr, "[%d] set_client_cache(): not full request\n", thread);
	}
	sharedData.cacheSync.unlock();
}

void set_server_cache(ConnectionDescriptor *connection, short &events) {
	pthread_t thread = pthread_self();
	if (!connection->connected) {
		fprintf(stderr, "[%d] set_server_cache(): not connected\n", thread);
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
			fprintf(stderr, "[%d] worker : [%d]\n", thread, connection->worker->socket);
			fprintf(stderr, "[%d] worker request length == %d\n", thread, connection->worker->requestLength);
			fprintf(stderr, "[%d] request length == %d\n", thread, connection->requestLength);
		} else {
			fprintf(stderr, "[%d] worker : [%d]\n", thread, connection->worker->socket);
		}
		assert(interval >= 0);
// update the request pointer, if the memory was reallocated
		connection->request = connection->worker->request;
		ssize_t len = ((interval) > BUFFER_SIZE) ?
					  BUFFER_SIZE : interval;
//        snprintf(connection.buffer, BUFFER_SIZE, "%.*s", len, &connection.request[connection.requestLength]);
// copy a part of the request to the buffer for sending
		memcpy(connection->buffer, &connection->request[connection->requestLength], len);
		connection->requestLength += len;
		connection->bsize = len;
//        if (connection.worker->fullRequest) {
//            if (interval <= BUFFER_SIZE) {
//                // if interval <= BUFFER_SIZE then it will be sent on the next poll handling
//                connection.fullRequest = true;
//            }
//        }
		if (connection->bsize > 0) { // has some data to send
			events |= POLLOUT;
		}
	}
}

// nocache versions were never used, done for next works
void set_client_nocache(ConnectionDescriptor *connection, short &events) {
	if (connection->fullRequest) { // has already received a full request from the client
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

void set_server_nocache(ConnectionDescriptor *connection, short &events) {
	if (!connection->connected) {
		events != POLLOUT;
		return;
	} else {
		events != POLLIN;
	}
	if (!connection->fullRequest) { // has not sent a full request yet
// transfer data from client buffer to server buffer
		if (connection->bsize == 0 && connection->worker->bsize > 0) {
			bzero(connection->buffer, sizeof(connection->buffer));
			memcpy(connection->buffer, connection->worker->buffer, connection->worker->bsize);
			connection->bsize = connection->worker->bsize;
			connection->worker->bsize = 0;
		}
		if (connection->bsize > 0) { // has some data to send
			events |= POLLOUT;
		}
	}
}

void drop_connection(Connections &connections, PollFDs &poll_fds, ConnectionDescriptor *connection, int *pos,
					 SharedData& sharedData) {
	int sock = connection->socket;
	pthread_t thread = pthread_self();
	std::string type = (connection->type == CLIENT) ? "client" : "server";
	fprintf(stderr, "[%d] drop_connection(): init %s [%d]\n", thread, type.c_str(), sock);
	if (connection->type == CLIENT) {
		sharedData.cacheSync.write_lock();
		if (connection->cacheKey != nullptr) {
			fprintf(stderr, "[%d] found cacheNode in cache\n", thread);
			auto &&cacheNode = sharedData.cache.find(*connection->cacheKey);
			assert(cacheNode != sharedData.cache.end());
			cacheNode->second.sync.write_lock();
			cacheNode->second.clientCounter--;
			if (cacheNode->second.clientCounter == 0 &&
				(cacheNode->second.downloading ||// remove if there's no other client on this page
					cacheNode->second.error)) { //and it hasn't downloaded yet or an error has occured
				fprintf(stderr, "[%d] removed from cache\n", thread);
				auto data = cacheNode->second.data;
				while (data != nullptr) {
					auto next = data->nextBlock;
					delete (data->buffer);
					delete (data);
					data = next;
				}
				cacheNode->second.endOfData = data;
				cacheNode->second.sync.unlock();
				sharedData.cache.erase(cacheNode->first);
			} else {
				cacheNode->second.sync.unlock();
			}
		}
		sharedData.cacheSync.unlock();
	} else {
		sharedData.cacheSync.read_lock();
		auto &&cacheNode = sharedData.cache.find(*connection->cacheKey);
		if (cacheNode != sharedData.cache.end()) {
			cacheNode->second.sync.write_lock();
			if (connection->error) {
				cacheNode->second.error = SERVER_ERR;
			}
			cacheNode->second.downloading = false;
			cacheNode->second.sync.unlock();
		}
		sharedData.cacheSync.unlock();
//        signal_all_threads();
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
	}
	if (connection->worker != nullptr) {
		connection->worker->worker = nullptr;
	}
	close(connection->socket);
	delete connection->cacheKey;
	free(connection->request);
	poll_fds.erase(poll_fds.begin() + *pos);
	(*pos)--;
	connections.erase(connection->socket);
	fprintf(stderr, "drop_connection(): end %s [%d]\n", type.c_str(), sock);
	delete (connection);
}

int init_connection_with_server(const char *host, Pair &cacheKey, ConnectionDescriptor *connection, SharedData& sharedData) {
	pthread_t thread = pthread_self();
	struct hostent *hp;
	int errnum;
	hp = getipnodebyname(host, AF_INET, AI_DEFAULT, &errnum);
	if (hp == nullptr) {
		switch (errnum) {
			case HOST_NOT_FOUND:
				fprintf(stderr, "[%d] init_connection_with_server(): "
								"Host is unknown.\n", thread);
				connection->error = WRONG_REQ;
				break;
			case NO_DATA:
				fprintf(stderr, "[%d] init_connection_with_server(): "
								"No address is available for the name specified in the server request.\n", thread);
				connection->error = WRONG_REQ;
				break;
			case NO_RECOVERY:
				fprintf(stderr, "[%d] init_connection_with_server(): "
								"An unexpected server failure occurred, which is a non-recoverable error.\n", thread);
				break;
			case TRY_AGAIN:
				fprintf(stderr, "[%d] init_connection_with_server(): "
								"Try again later.\n", thread);
				break;
			default:
				fprintf(stderr, "[%d] init_connection_with_server(): "
								"Unknown error.\n", thread);
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
		fprintf(stderr, "[%d] init_connection_with_server(): socket() error: %s\n", thread, strerror(errno));
		return SERVER_ERR;
	}
	fcntl(serverSocket, F_SETFL, O_NONBLOCK);
	if (connect(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
		if (errno != EINPROGRESS && errno != EALREADY) {
			fprintf(stderr, "[%d] init_connection_with_server(): connect() error: %s\n", thread, strerror(errno));
			return SERVER_ERR;
		}
	}
//    auto && page = cache.find(*cacheKey);
	auto *serverConnection = new ConnectionDescriptor;
	serverConnection->socket = serverSocket;
	serverConnection->cacheKey = new Pair(cacheKey);
	fprintf(stderr, "[%d] init_connection_with_server(): server cacheKey: %s\n",
			thread, string_cache_key(serverConnection->cacheKey).c_str());
//    auto w = find_descriptor(connections, connection->socket);
//    serverConnection->worker = w;
	serverConnection->worker = connection;
	serverConnection->type = SERVER;
	serverConnection->connected = false;
	serverConnection->status = CONNECTED;
	connection->worker = serverConnection;
	sharedData.queueSync.m_lock();
	sharedData.threadToQueue[sharedData.cache[cacheKey].thread].push(serverConnection);
	sharedData.queueSync.m_unlock();
	return SUCCESS;
}

int try_to_parse_headers(Connections &connections, PollFDs &poll_fds,
						 ConnectionDescriptor *connection, pthread_t threads[],
						 SharedData& sharedData) {
	pthread_t thread = pthread_self();
//    Str headers= connection.buffer;
	std::string headers;
	if (connection->request == nullptr) {
		fprintf(stderr, "\n[Main] request is nullptr\n");
		return NOT_READY;
	}
	headers.assign(connection->request);
	int pos;
	pos = headers.find("GET", 0, connection->bsize);
	if (pos == std::string::npos) { //not found, method != GET
		if (connection->bsize > 3) {
			fprintf(stderr, "\n[Main] try_to_parse_headers(): Unsupported method\n");
			connection->error = UNSUPPORTED_METH;
			return DROP;
		}
		fprintf(stderr, "\n[Main] Not found method \"GET\"\n");
		return NOT_READY;
	} else {
		pos += 4;
		int end = headers.find_first_of(' ', pos);
		if (end != std::string::npos) {
			Pair resource;
			std::string uri = headers.substr(pos, end - pos);
//            Str host = headers.substr(hb, he-hb);
			std::string host = find_host(headers);
			if (host.empty()) {
				fprintf(stderr, "\n[Main] try_to_parse_headers(): Host is empty\n");
				return NOT_READY;
			} else {
				fprintf(stderr, "\n[Main] try_to_parse_headers(): Host == %s\n", host.c_str());
			}
			if (check_protocol(headers, connection->request) != SUCCESS) {
				fprintf(stderr, "\n[Main] try_to_parse_headers(): Wrong protocol\n");
				connection->error = WRONG_VER;
				return DROP;
			}
			if (headers.find("\r\n\r\n") != std::string::npos) {
				connection->fullRequest = true;
			} else {
				return NOT_READY;
			}
			resource = {host, uri};
			Cache::iterator iter;
			sharedData.cacheSync.write_lock();
			if ((iter = sharedData.cache.find(resource)) == sharedData.cache.end()) { //no such record in cache
				static int num = 0;
				PageDescriptor page;
				page.clientCounter++;
				page.thread = threads[num];
				num = (num + 1) % sharedData.thread_pool_size;
				auto result = sharedData.cache.insert({resource, page});
				fprintf(stderr, "\n[Main] try_to_parse_headers(): try to init connection with server\n");
				if (!result.second || init_connection_with_server(
					host.c_str(), resource, connection, sharedData) != SUCCESS) {
					fprintf(stderr, "\n[Main] try_to_parse_headers(): Cannot init connection with server\n");
					sharedData.cache.erase(resource);
					if (!connection->error) {
						connection->error = SERVER_ERR;
					}
					sharedData.cacheSync.unlock();
					return DROP;
				} else {
					connection->cacheKey = new Pair(resource);
				}
			} else {
				fprintf(stderr, "\n[Main] try_to_parse_headers(): already in cache\n");
				(*iter).second.clientCounter++;
				connection->cacheKey = new Pair(resource);
			}

			sharedData.cacheSync.unlock();
			fprintf(stderr, "[Main] Got request:\n"
							"\"%s\"\n"
							"Host == %s\n"
							"URI == %s\n", headers.c_str(), host.c_str(), uri.c_str());
			return SUCCESS;
		} else {
			return NOT_READY;
		}
	}
}