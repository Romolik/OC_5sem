#include <iostream>
#include <map>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <climits>
#include <cassert>

#include "Connection.h"

#define WRONG_INPUT "wrong input parameters. \
Required (int) port"
#define NO_LIMIT (-1)
#define POLL_TIMEOUT NO_LIMIT
#define MAX_SIZE_PAGE 60*1024*1024

int set_poll_arguments(Cache& cache, PollFDs& poll_fds, Connections& connections);

int handle_poll_events(Cache& cache, PollFDs& poll_fds, Connections& connections);

void sig_handler(int) {
	exit(1);
}

long long int sizePage = 0;
long long int sizeOtdat = 0;

enum CliArguments {
  NUMBER_PORT = 0,

  REQUIRED_COUNT
};

int main(int argc, char **argv) {
	if (argc < CliArguments::REQUIRED_COUNT + 1) {
		fprintf(stderr, WRONG_INPUT "\n");
		return EXIT_FAILURE;
	}
	long int port;
	struct sockaddr_in listen_addr;
	Cache cache;
	PollFDs poll_fds;
	Connections connections;
	int listenSocket;
	const int BASE = 10;
	const int MAX_PORT_NUMBER = 65535;
	port = strtol (argv[CliArguments::NUMBER_PORT  + 1],(char**)NULL, BASE);
	if (LONG_MIN == port) {
		perror("the conversion result was extremely small");
		return EXIT_FAILURE;
	} else if (LONG_MAX == port) {
		perror("an overflow has occurred");
		return EXIT_FAILURE;
	}
	if (port < 0 || port > MAX_PORT_NUMBER) {
		perror("Error number port");
		return EXIT_FAILURE;
	}

	if ((listenSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("listening socket create error");
		return EXIT_FAILURE;
	}

	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	listen_addr.sin_port = htons(port);
	if (bind(listenSocket, (struct sockaddr *) &listen_addr, sizeof(listen_addr)) < 0) {
		perror("listening socket bind error");
		if (shutdown(listenSocket, SHUT_RDWR) < 0) {
			perror("error shutdown socket");
		}
		if (close(listenSocket) < 0) {
			perror("error close socket");
		}
		return EXIT_FAILURE;
	}

	const int backlog = 510;
	if (listen(listenSocket, backlog) < 0) {
		perror("listen error");
		if (shutdown(listenSocket, SHUT_RDWR) < 0) {
			perror("error shutdown socket");
		}
		if (close(listenSocket) < 0) {
			perror("error close socket");
		}
		return EXIT_FAILURE;
	}

	fprintf(stderr, "started listening\n");
	sigset(SIGPIPE, SIG_IGN);
	sigset(SIGINT, sig_handler);

	poll_fds.push_back({listenSocket, POLLIN, 0});
	while (true) {
		int res = set_poll_arguments(cache, poll_fds, connections);
		if (res == FAILURE) {
			if (shutdown(listenSocket, SHUT_RDWR) < 0) {
				perror("error shutdown socket");
			}
			if (close(listenSocket) < 0) {
				perror("error close socket");
			}
			return EXIT_FAILURE;
		}
		int ready;
		fprintf(stderr, "before poll, nfds == %ld\n", poll_fds.size());
		if ((ready = poll(&poll_fds[0], poll_fds.size(), POLL_TIMEOUT)) < 0) {
			perror("poll error");
			if (shutdown(listenSocket, SHUT_RDWR) < 0) {
				perror("error shutdown socket");
			}
			if (close(listenSocket) < 0) {
				perror("error close socket");
			}
			return EXIT_FAILURE;
		}
		fprintf(stderr, "after poll, ready : %d\n", ready);
		res = handle_poll_events(cache, poll_fds, connections);
		if (res == FAILURE) {
			if (shutdown(listenSocket, SHUT_RDWR) < 0) {
				perror("error shutdown socket");
			}
			if (close(listenSocket) < 0) {
				perror("error close socket");
			}
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

int set_poll_arguments(Cache& cache, PollFDs& poll_fds, Connections& connections) {
	for (long unsigned int i = 1; i < poll_fds.size(); i++) {
		auto connection = connections[poll_fds[i].fd];
		if (connection->status == WAITING) {
			fprintf(stderr, "set_poll_arguments(): ");
			poll_fds[i].events = 0;
			if (!(connection)->error) {
				int res = try_to_parse_headers(connection, cache, poll_fds, connections);
				switch (res) {
					case SUCCESS:
						fprintf(stderr, "waiting[%d] : OK\n", (connection)->socket);
						connection->status = CONNECTED;
						break;
					case DROP:
						fprintf(stderr, "waiting[%d] : DROP\n", (connection)->socket);
						poll_fds[i].events |= POLLOUT;
						break;
					case NOT_READY:
						fprintf(stderr, "waiting[%d] : NOT READY\n", (connection)->socket);
						fprintf(stderr, "msg: \"%ld ", (connection)->requestLength);
						fprintf(stderr, "%s\"\n", (connection)->request);
						poll_fds[i].events |= POLLIN;
						break;
					default:
						perror("error switch connection");
				}
			} else {
				fprintf(stderr, "waiting[%d] : ERROR\n", (connection)->socket);
				poll_fds[i].events |= POLLOUT;
			}
		}
		fprintf(stderr, "set_poll_arguments(): waiting: done\n");
		if (connection->status == CONNECTED) {
			fprintf(stderr, "set_poll_arguments(): connection type == %s\n",
					((connection)->type == CLIENT) ? "client" : "server");
			fprintf(stderr, "set_poll_arguments(): connection cacheKey: %s\n",
					string_cache_key((connection)->cacheKey).c_str());
			auto &&cachePage = cache.find(*(connection)->cacheKey);
			if (cachePage == cache.end()) {
				fprintf(stderr, "set_poll_arguments(): not found in cache\n");
				if ((connection)->type == SERVER) {
					drop_connection(connection, &i, cache, poll_fds, connections);
					continue;
				} else {
					return FAILURE;
				}
			}
			poll_fds[i].events = 0;
			if ((connection)->type == CLIENT) {
				poll_fds[i].events |= POLLIN;// to check if the socket has been closed
			}
			if ((connection)->useCache) {
				if ((connection)->type == CLIENT) {
					if (FAILURE == set_client_cache(connection, poll_fds[i].events, cache)) {
						return FAILURE;
					}
				}
				if ((connection)->type == SERVER) {
					set_server_cache(connection, poll_fds[i].events);
				}
			} else {
				if ((connection)->type == CLIENT) {
					set_client_nocache(connection, poll_fds[i].events);
				}
				if ((connection)->type == SERVER) {
					set_server_nocache(connection, poll_fds[i].events);
				}
			}
		}
		fprintf(stderr, "set_poll_arguments(): connected: done\n");
	}
	return SUCCESS;
}

int handle_poll_events(Cache& cache, PollFDs& poll_fds, Connections& connections) {
	fprintf(stderr, "handle_poll_events(): begin\n");
	if (poll_fds[0].revents & POLLIN) {
		int clientSocket;
		struct sockaddr_in clientAddr;
		unsigned int addrLen = sizeof(clientAddr);
		clientSocket = accept(poll_fds[0].fd, (struct sockaddr *) &clientAddr, &addrLen);
		if (clientSocket < 0) {
			perror("handle_poll_events(): accept error");
			return FAILURE;
		}
		fprintf(stderr, "handle_poll_events(): add new connection: [%d]\n", clientSocket);
		add_connection(clientSocket, poll_fds, connections);
	}

	for (long unsigned int i = 1; i < poll_fds.size(); i++) {
		auto connection = connections[poll_fds[i].fd];
		if (connection->status == WAITING) {
			fprintf(stderr, "handle_poll_events(): found waiting connection: [%d]\n", (connection)->socket);
			if (poll_fds[i].revents & POLLOUT) {
				fprintf(stderr, "WAITING POLLOUT\n");
				if ((connection)->error) {
					fprintf(stderr, "handle_poll_events(): drop connection: [%d]\n", (connection)->socket);
					drop_connection(connection, &i, cache, poll_fds, connections);
					continue;
				} else {
					assert(false);
				}
			}
			if (poll_fds[i].revents & POLLIN) {
				fprintf(stderr, "WAITING POLLIN\n");
				if ((connection)->bsize == 0) {
					if (((connection)->bsize = read((connection)->socket, (connection)->buffer, BUFFER_SIZE)) < 0) {
						std::string errorStr = "handle_poll_events(): read from connection error";
						perror(errorStr.c_str());
						(connection)->error = SERVER_ERR;
						drop_connection(connection, &i, cache, poll_fds, connections);
						continue;
					}
					if ((connection)->bsize == 0) {
						fprintf(stderr, "handle_poll_events(): drop connection: [%d]\n", (connection)->socket);
						drop_connection(connection, &i, cache, poll_fds, connections);
						continue;
					}
					if (!(connection)->fullRequest) {
						char* req =  (char *) realloc((connection)->request,
													  (connection)->requestLength + (connection)->bsize);
						if (NULL == req) {
							fprintf(stderr, "handle_poll_events(): drop connection: [%d]\n", (connection)->socket);
							drop_connection(connection, &i, cache, poll_fds, connections);
							continue;
						} else {
							(connection)->request = req;
						}
						memcpy(&(connection)->request[(connection)->requestLength],
							   (connection)->buffer, (connection)->bsize);
						(connection)->requestLength += (connection)->bsize;
					}
					(connection)->bsize = 0; // just ignoring all messages from the client after a full request
				}
			}
		} else if (connection->status == CONNECTED) {
			fprintf(stderr, "handle_poll_events(): found connected connection: [%d]\n", (connection)->socket);
			if ((connection)->type == SERVER) {
				fprintf(stderr, "\nENTER\n");
				if (poll_fds[i].revents & POLLOUT && !connection->fullRequest) {
					connection->connected = true;
					fprintf(stderr, "SERVER POLLOUT\n");
					std::string tmp = (connection)->buffer;
					const long unsigned int tmp2 = tmp.find("Range:");
					if (connection->bsize > 0) {
                                              if (tmp2 != std::string::npos) {
                                                      tmp = tmp.substr(0, tmp2);
                                                      (connection)->bsize = tmp2;
                                              }
						int res;
						res = write((connection)->socket, (connection)->buffer, (connection)->bsize);
						if (tmp2 != std::string::npos) {
							 write((connection)->socket, "\r\n\r\n", 4);
						}
						fprintf(stderr, "wrote to server: %d\n", res);
						if (res < 0) {
							perror("handle_poll_events(): server: write to socket");
							(connection)->error = SERVER_ERR;
							drop_connection(connection, &i, cache, poll_fds, connections);
							continue;
						}
						assert(res == (connection)->bsize);
						(connection)->bsize = 0;
					}
				}
				if (poll_fds[i].revents & POLLIN) {
					fprintf(stderr, "SERVER POLLIN\n");
					char tmp[BUFFER_SIZE];
					int res;
					res = read((connection)->socket, tmp, BUFFER_SIZE);
					fprintf(stderr, "read from server: %d\n", res);
					sizePage+=res;
					fprintf(stderr, "\n\nSIZE_PAGE %lld\n\n", sizePage);
					auto cacheNode = cache.find(*(connection)->cacheKey);
					if (res <= 0 || cacheNode == cache.end()) {
						if (res < 0) {
							if (errno == EAGAIN) {
								continue;
							}
							perror("handle_poll_events(): server: read from socket");
							(connection)->error = SERVER_ERR;
							drop_connection(connection, &i, cache, poll_fds, connections);
							continue;
						}
						if (connection->bsize == 0 || connection->worker == nullptr) {
							drop_connection(connection, &i, cache, poll_fds, connections);
						}
						continue;
					}
					auto page = &cacheNode->second;
					DataBlock* dataBlock;
					try {
						dataBlock = new DataBlock;
						dataBlock->buffer = new char[res];
					} catch (const std::bad_alloc &e) {
						std::cout << e.what() << '\n';
						throw;
					}

					memcpy(dataBlock->buffer, tmp, res);
					dataBlock->bsize = res;
					long long int len = find_headers_length(dataBlock->buffer, res);
					if (FAILURE != len) {
						if (MAX_SIZE_PAGE < len) {
							cacheNode->second.useCache = false;
							connection->useCache = false;
							connection->worker->useCache = false;
						}
					}
					if (connection->useCache == true) {
						if (page->data == nullptr) {
							page->data = page->endOfData = dataBlock;
						} else {
							page->endOfData->nextBlock = dataBlock;
							page->endOfData = dataBlock;
						}
						page->size += res;//???
					} else {
						if (connection->worker == nullptr) {
							drop_connection(connection, &i, cache, poll_fds, connections);
						}
						memcpy(&(connection)->buffer, dataBlock->buffer, res);
						connection->bsize = res;
						delete dataBlock;
					}
				}
				fprintf(stderr, "\nEXIT\n");
			} else { // CLIENT
				if (poll_fds[i].revents & POLLIN) {
					fprintf(stderr, "CLIENT POLLIN\n");
					char tmp[BUFFER_SIZE];
					int res;
					res = read((connection)->socket, tmp, BUFFER_SIZE);
					fprintf(stderr, "read from client: %d\n", res);
					if (res <= 0) {
						if (res < 0) {
							perror("handle_poll_events(): client: read from socket");
							(connection)->error = SERVER_ERR;
						}
						drop_connection(connection, &i, cache, poll_fds, connections);
						continue;
					}
					if (!(connection)->fullRequest) {
						char* req = (char *) realloc((connection)->request,(connection)->requestLength + res);
						if (NULL == req) {
							fprintf(stderr, "handle_poll_events(): drop connection: [%d]\n", (connection)->socket);
							drop_connection(connection, &i, cache, poll_fds, connections);
							continue;
						} else {
							(connection)->request = req;
						}
						memcpy(&(connection)->request[(connection)->requestLength], tmp, res);
						(connection)->requestLength += res;
						check_request(connection);
					}
				}
				if (poll_fds[i].revents & POLLOUT) {
					fprintf(stderr, "CLIENT POLLOUT\n");
					if ((connection)->error) {
						fprintf(stderr, "handle_poll_events(): drop connection: [%d]\n", (connection)->socket);
						drop_connection(connection, &i, cache, poll_fds, connections);
						continue;
					} else {
						if (connection->useCache == true) {
							auto &&cacheNode = cache.find(*(connection)->cacheKey);
							if (cacheNode != cache.end()) {
								auto &page = cacheNode->second;
								if ((connection)->currentBlock == nullptr) {
									(connection)->currentBlock = page.data;
								}
								if ((connection)->currentBlock != page.endOfData) {
									int res;
									res = write((connection)->socket, (connection)->currentBlock->buffer,
												(connection)->currentBlock->bsize);
									fprintf(stderr, "wrote to client: %d\n", res);
									if (res < 0) {
										perror("handle_poll_events(): client: write ro socket");
										(connection)->error = SERVER_ERR;
										drop_connection(connection, &i, cache, poll_fds, connections);
										continue;
									}
									assert(res == (connection)->currentBlock->bsize);
									(connection)->currentBlock = (connection)->currentBlock->nextBlock;
								} else { // last block of data
									int res;
									res = write((connection)->socket, (connection)->currentBlock->buffer,
												(connection)->currentBlock->bsize);
									fprintf(stderr, "wrote to client: %d\n", res);
									if (res < 0) {
										perror("handle_poll_events(): client: write ro socket");
										(connection)->error = SERVER_ERR;
										drop_connection(connection, &i, cache, poll_fds, connections);
										continue;
									}
									assert(res == (connection)->currentBlock->bsize);
									assert(!page.downloading);
									// the server closed the connection
									drop_connection(connection, &i, cache, poll_fds, connections);
								}
							} else {
								assert(false);
							}
						} else {
							int res;
							res = write((connection)->socket, (connection)->buffer,
										(connection)->bsize);
							sizeOtdat+=res;
							fprintf(stderr, "\n\nsizeOtdat %lld\n\n", sizeOtdat);
							fprintf(stderr, "wrote to client: %d\n", res);
							if (res < 0) {
								perror("handle_poll_events(): client: write to socket");
								(connection)->error = SERVER_ERR;
								drop_connection(connection, &i, cache, poll_fds, connections);
								continue;
							}
							assert(res == (connection)->bsize);
							(connection)->bsize = 0;
						}

					}
				}
			}
		}
	}
	fprintf(stderr, "handle_poll_events(): end\n");
	return SUCCESS;
}