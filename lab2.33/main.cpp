#include <iostream>
#include <map>
#include <poll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <cassert>
#include <strings.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <climits>

#include "Connection.h"

#define WRONG_INPUT "wrong input parameters. \
Required (int) port, (int) threads number"
#define NO_LIMIT (-1)
#define POLL_TIMEOUT NO_LIMIT

enum CliArguments {
  NUMBER_PORT = 0,
  NUMBER_THREADS = 1,
  REQUIRED_COUNT
};

int set_poll_arguments(Connections &connections, PollFDs &poll_fds, SharedData& sharedData);

void handle_poll_events(Connections &connections, PollFDs &poll_fds, SharedData& sharedData);


void sig_handler(int) {
	exit(1);
}

void *worker_thread(void *args);

void set_poll_args_main(Connections &connections, PollFDs &poll_fds, pthread_t threads[], SharedData& sharedData);

void handle_poll_main(Connections &connections, PollFDs &poll_fds, SharedData& sharedData);

int main(int argc, char **argv) {
	if (argc < CliArguments::REQUIRED_COUNT + 1) {
		fprintf(stderr, WRONG_INPUT "\n");
		return EXIT_FAILURE;
	}
	int port, threadsNum;
	struct sockaddr_in listen_addr;
	int listenSocket;
	const int BASE = 10;
	const int MAX_PORT_NUMBER = 65535;
	const int MAX_THREADS_NUMBER = 100;
	port = strtol (argv[CliArguments::NUMBER_PORT  + 1],(char**)NULL, BASE);
	if (LONG_MIN == port) {
		perror("the conversion result was extremely small");
		return EXIT_FAILURE;
	} else if (LONG_MAX == port) {
		perror("an overflow has occurred");
		return EXIT_FAILURE;
	}
	if (port <= 0 || port > MAX_PORT_NUMBER) {
		perror("Error number port");
		return EXIT_FAILURE;
	}
	threadsNum = strtol (argv[CliArguments::NUMBER_THREADS  + 1],(char**)NULL, BASE);
	if (LONG_MIN == threadsNum) {
		perror("the conversion result was extremely small");
		return EXIT_FAILURE;
	} else if (LONG_MAX == threadsNum) {
		perror("an overflow has occurred");
		return EXIT_FAILURE;
	}
	if (threadsNum <= 0 || threadsNum > MAX_THREADS_NUMBER) {
		perror("Error number threads");
		return EXIT_FAILURE;
	}

	SharedData sharedData;
	sharedData.thread_pool_size = threadsNum;
	if ((listenSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("listening socket create error");
		return EXIT_FAILURE;
	}
	int res;
	do {
		res = pthread_barrier_init(&sharedData.queue_barrier, nullptr, threadsNum + 1);
		if (res < 0 && res != EAGAIN) {
			fprintf(stderr, "barrier init error: %s\n", strerror(res));
			return EXIT_FAILURE;
		}
	} while (res == EAGAIN);

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
	listen(listenSocket, backlog);
	sigset(SIGPIPE, SIG_IGN);
	sigset(SIGINT, sig_handler);

	pthread_t thread[threadsNum];
	for (int i = 0; i < threadsNum; i++) {
		int status;
		res = pipe(sharedData.filedes);
		if (res < 0) {
			if (shutdown(listenSocket, SHUT_RDWR) < 0) {
				perror("error shutdown socket");
			}
			if (close(listenSocket) < 0) {
				perror("error close socket");
			}
			return EXIT_FAILURE;
		}
		if ((status = pthread_create(&thread[i], nullptr, worker_thread, &sharedData)) != 0) {
			fprintf(stderr, "can't create thread: %s\n", strerror(status));
			return EXIT_FAILURE;
		} else {
			sharedData.threadToPipe[thread[i]] = sharedData.filedes[1];
			pthread_detach(thread[i]);
		}
	}
	pthread_barrier_wait(&sharedData.queue_barrier);
	fprintf(stderr, "started listening\n");
	Connections waiting_connections;
	PollFDs poll_fds;
	poll_fds.push_back({listenSocket, POLLIN, 0});
	while (true) {
		set_poll_args_main(waiting_connections, poll_fds, thread, sharedData);
		int ready;
//        fprintf(stderr, "before poll, nfds == %d\n", poll_fds.size());
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
		handle_poll_main(waiting_connections, poll_fds, sharedData);
	}
	return SUCCESS;
}

void *worker_thread(void *args) {
	SharedData *sharedData = (SharedData *) args;
	int pipe_fd = *(&sharedData->filedes[0]);
	fcntl(pipe_fd, F_SETFL, O_NONBLOCK);
	pthread_t thread = pthread_self();
	Connections connections;
	PollFDs poll_fds;
	poll_fds.push_back({pipe_fd, POLLIN, 0});
	pthread_barrier_wait(&sharedData->queue_barrier);
	fprintf(stderr, "worker[%d]: got through barrier\n", thread);
	while (true) {
		sharedData->queueSync.m_lock();
		fprintf(stderr, "worker[%d]: locked queue mutex\n", thread);
		while (connections.empty() && sharedData->threadToQueue[thread].empty()) {
			fprintf(stderr, "worker[%d]: waiting\n", thread);
			if (sharedData->threadToQueue[thread].empty()) {
				fprintf(stderr, "worker[%d]: connection queue is empty\n", thread);
			}
			sharedData->queueSync.wait();
		}
		while (!sharedData->threadToQueue[thread].empty()) {
			auto connection = sharedData->threadToQueue[thread].front();
			connections[connection->socket] = connection;
			poll_fds.push_back({connection->socket, POLLIN, 0});
			sharedData->threadToQueue[thread].pop();
			fprintf(stderr, "worker[%d]: got new connection from queue\n", thread);
		}
		fprintf(stderr, "worker[%d]: task queue is empty\n", thread);
		sharedData->queueSync.m_unlock();
		if (FAILURE == set_poll_arguments(connections, poll_fds, *sharedData)) {
			return NULL;
		}
		int ready;
		fprintf(stderr, "before poll, nfds == %d\n", poll_fds.size());
		if ((ready = poll(&poll_fds[0], poll_fds.size(), POLL_TIMEOUT)) < 0) {
			perror("poll error");
			return NULL;
		}
//        fprintf(stderr, "after poll, ready : %d\n", ready);
		handle_poll_events(connections, poll_fds, *sharedData);
	}
}

void share_connection(Connections &connections, PollFDs &poll_fds,
					  ConnectionDescriptor *connection, int *position, pthread_t &thread,
					  SharedData& sharedData) {
	connections.erase(connection->socket);
	sharedData.queueSync.m_lock();
	sharedData.threadToQueue[thread].push(connection);
	sharedData.queueSync.broadcast();
	sharedData.queueSync.m_unlock();
	poll_fds.erase(poll_fds.begin() + *position);
	(*position)--;
	write(sharedData.threadToPipe[thread], "1", 1);
}

void set_poll_args_main(Connections &connections, PollFDs &poll_fds, pthread_t threads[], SharedData& sharedData) {
	for (int i = 1; i < poll_fds.size(); i++) {
		auto connection = connections[poll_fds[i].fd];
		assert (connection->status == WAITING);
		poll_fds[i].events = 0;
		if (!(connection)->error) {
			int res = try_to_parse_headers(connections, poll_fds, connection, threads, sharedData);
			pthread_t thread;
			switch (res) {
				case SUCCESS:
					fprintf(stderr, "[Main] set_poll_arguments(): waiting[%d] : OK\n", (connection)->socket);
					connection->status = CONNECTED;
					sharedData.cacheSync.read_lock();
					thread = sharedData.cache[*(connection->cacheKey)].thread;
					sharedData.cacheSync.unlock();
					share_connection(connections, poll_fds, connection, &i, thread, sharedData);
					break;
				case DROP:
					fprintf(stderr, "[Main] set_poll_arguments(): waiting[%d] : DROP\n", (connection)->socket);
					poll_fds[i].events |= POLLOUT;
					break;
				case NOT_READY:
					fprintf(stderr, "[Main] set_poll_arguments(): waiting[%d] : NOT READY\n", (connection)->socket);
					fprintf(stderr, "[Main] msg: \"%.*s\"\n",
							(connection)->requestLength, (connection)->request);
					poll_fds[i].events |= POLLIN;
					break;
			}
		} else {
			fprintf(stderr, "[Main] set_poll_arguments(): waiting[%d] : ERROR\n", (connection)->socket);
			poll_fds[i].events |= POLLOUT;
		}
		fprintf(stderr, "[Main] set_poll_arguments(): waiting: done\n");
	}
}

int set_poll_arguments(Connections &connections, PollFDs &poll_fds, SharedData& sharedData) {
	pthread_t thread = pthread_self();
	for (int i = 1; i < poll_fds.size(); i++) {
		auto connection = connections[poll_fds[i].fd];
		assert (connection->status == CONNECTED);
		fprintf(stderr, "[%d] set_poll_arguments(): connection type == %s\n",
				thread, ((connection)->type == CLIENT) ? "client" : "server");
//            fprintf(stderr, "set_poll_arguments(): connection cacheKey: %s\n",
//                    string_cache_key((connection)->cacheKey).c_str());
		sharedData.cacheSync.read_lock();
		auto &&cachePage = sharedData.cache.find(*(connection)->cacheKey);
		if (cachePage == sharedData.cache.end()) {
			sharedData.cacheSync.unlock();
			fprintf(stderr, "[%d] set_poll_arguments(): not found in cache\n", thread);
			if ((connection)->type == SERVER) {
				drop_connection(connections, poll_fds, connection, &i, sharedData);
				continue;
			} else {
				return FAILURE;
			}
		}
		poll_fds[i].events = 0;
		(connection)->useCache = cachePage->second.useCache;
		sharedData.cacheSync.unlock();
		if ((connection)->type == CLIENT) {
			poll_fds[i].events |= POLLIN;// to check if the socket has been closed
		}
		if ((connection)->useCache) {
			if ((connection)->type == CLIENT) {
				set_client_cache(connection, poll_fds[i].events, sharedData);
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
		fprintf(stderr, "[%d] set_poll_arguments(): connected: done\n", thread);
	}
	return SUCCESS;
}

void handle_poll_events(Connections &connections, PollFDs &poll_fds, SharedData& sharedData) {
	pthread_t thread = pthread_self();
	fprintf(stderr, "[%d] handle_poll_events(): begin\n", thread);
	if (poll_fds[0].revents & POLLIN) {
		char tmp[2 * sharedData.thread_pool_size];
		read(poll_fds[0].fd, tmp, 2 * sharedData.thread_pool_size);
		poll_fds[0].revents = 0;
	}
//    ConnectionDescriptor *connection;
	for (int i = 1; i < poll_fds.size(); i++) {
		auto connection = connections[poll_fds[i].fd];
		assert (connection->status == CONNECTED);
		fprintf(stderr, "[%d] handle_poll_events(): found connected connection: [%d]\n", thread,
				(connection)->socket);
		if ((connection)->type == SERVER) {
			if (poll_fds[i].revents & POLLOUT) {
				connection->connected = true;
				fprintf(stderr, "[%d] handle_poll_events(): SERVER POLLOUT\n", thread);
				if (connection->bsize > 0) {
					int res;
					res = write((connection)->socket, (connection)->buffer, (connection)->bsize);
					fprintf(stderr, "[%d] wrote to server: %d\n", thread, res);
					if (res < 0) {
						fprintf(stderr, "[%d] handle_poll_events(): server: write to socket error: %s\n",
								thread, strerror(errno));
						(connection)->error = SERVER_ERR;
						drop_connection(connections, poll_fds, connection, &i, sharedData);
						continue;
					}
					assert(res == (connection)->bsize);
					(connection)->bsize = 0;
				}
			}
			if (poll_fds[i].revents & POLLIN) {
				fprintf(stderr, "[%d] handle_poll_events(): SERVER POLLIN\n", thread);
				char tmp[BUFFER_SIZE];
				int res;
				res = read((connection)->socket, tmp, BUFFER_SIZE);
//                    fprintf(stderr, "read from server: %d\n", res);
				sharedData.cacheSync.read_lock();
				auto cacheNode = sharedData.cache.find(*(connection)->cacheKey);
				if (res <= 0 || cacheNode == sharedData.cache.end()) {
					if (res < 0) {
						if (errno == EAGAIN) {
							sharedData.cacheSync.unlock();
							continue;
						}
						fprintf(stderr, "[%d] handle_poll_events(): server: read from socket", thread);
						(connection)->error = SERVER_ERR;
					}
					sharedData.cacheSync.unlock();
					drop_connection(connections, poll_fds, connection, &i, sharedData);
					continue;
				}
//                signal_all_threads();
				auto page = &cacheNode->second;
				page->sync.write_lock();
				auto dataBlock = new DataBlock;
				dataBlock->buffer = new char[res];
				memcpy(dataBlock->buffer, tmp, res);
				dataBlock->bsize = res;
				if (page->data == nullptr) {
					page->data = page->endOfData = dataBlock;
//                        ssize_t len;
//                        if((len = find_length(tmp, res)) != -1) {
//                            page->length = len;
//                        }
				} else {
					page->endOfData->nextBlock = dataBlock;
					page->endOfData = dataBlock;
				}
				page->size += res;
				page->sync.unlock();
				sharedData.cacheSync.unlock();
			}
		} else { // CLIENT
			if (poll_fds[i].revents & POLLIN) {
				fprintf(stderr, "[%d] handle_poll_events(): CLIENT POLLIN\n", thread);
				char tmp[BUFFER_SIZE];
				int res;
				res = read((connection)->socket, tmp, BUFFER_SIZE);
				fprintf(stderr, "read from client: %d\n", res);

				if (res <= 0) {
					if (res < 0) {
						fprintf(stderr, "[%d] handle_poll_events(): client: read from socket",
								thread, strerror(errno));
						(connection)->error = SERVER_ERR;
					}
					drop_connection(connections, poll_fds, connection, &i, sharedData);
					continue;
				}
				if (!(connection)->fullRequest) {
					(connection)->request = (char *) realloc((connection)->request,
															 (connection)->requestLength + res);
					memcpy(&(connection)->request[(connection)->requestLength], tmp, res);
					(connection)->requestLength += res;
					check_request(connection);
				}
			}
			if (poll_fds[i].revents & POLLOUT) {
				fprintf(stderr, "[%d] handle_poll_events(): CLIENT POLLOUT\n", thread);
				if ((connection)->error) {
//                        fprintf(stderr, "[%d] handle_poll_events(): drop connection: [%d]\n", thread, (connection)->socket);
					drop_connection(connections, poll_fds, connection, &i, sharedData);
					continue;
				} else {
					sharedData.cacheSync.read_lock();
					auto &&cacheNode = sharedData.cache.find(*(connection)->cacheKey);
					if (cacheNode != sharedData.cache.end()) {
						auto &page = cacheNode->second;
						page.sync.read_lock();
						if ((connection)->currentBlock == nullptr) {
							(connection)->currentBlock = page.data;
						}
						if ((connection)->currentBlock != page.endOfData) {
							int res;
							res = write((connection)->socket, (connection)->currentBlock->buffer,
										(connection)->currentBlock->bsize);
//                                fprintf(stderr, "wrote to client: %d\n", res);
							if (res < 0) {
								fprintf(stderr, "[%d] handle_poll_events(): "
												"client: write to socket error: %s\n", thread, strerror(errno));
								(connection)->error = SERVER_ERR;
								page.sync.unlock();
								sharedData.cacheSync.unlock();
								drop_connection(connections, poll_fds, connection, &i, sharedData);
								continue;
							}
							assert(res == (connection)->currentBlock->bsize);
							(connection)->currentBlock = (connection)->currentBlock->nextBlock;
							page.sync.unlock();
							sharedData.cacheSync.unlock();
						} else { // last block of data
							int res;
							res = write((connection)->socket, (connection)->currentBlock->buffer,
										(connection)->currentBlock->bsize);
//                                fprintf(stderr, "wrote to client: %d\n", res);
							if (res < 0) {
								fprintf(stderr, "[%d] handle_poll_events(): "
												"client: write to socket error: %s\n", thread, strerror(errno));
								(connection)->error = SERVER_ERR;
								page.sync.unlock();
								sharedData.cacheSync.unlock();
								drop_connection(connections, poll_fds, connection, &i, sharedData);
								continue;
							}
							assert(res == (connection)->currentBlock->bsize);
							assert(!page.downloading);
// the server closed the connection
							page.sync.unlock();
							sharedData.cacheSync.unlock();
							drop_connection(connections, poll_fds, connection, &i, sharedData);
						}
					} else {
						sharedData.cacheSync.unlock();
						assert(false);
					}
				}
			}
		}
	}
	fprintf(stderr, "[%d] handle_poll_events(): end\n", thread);
}

void handle_poll_main(Connections &connections, PollFDs &poll_fds, SharedData& sharedData) {
	if (poll_fds[0].revents & POLLIN) {
		int clientSocket;
		struct sockaddr_in clientAddr;
		unsigned int addrLen = sizeof(clientAddr);
		clientSocket = accept(poll_fds[0].fd, (struct sockaddr *) &clientAddr, &addrLen);
		if (clientSocket < 0) {
			perror("handle_poll_events(): accept error");
			exit(7);
		}
		fprintf(stderr, "handle_poll_events(): add new connection: [%d]\n", clientSocket);
		add_connection(connections, poll_fds, clientSocket);
	}
	for (int i = 1; i < poll_fds.size(); i++) {
		auto connection = connections[poll_fds[i].fd];
		if (connection->status == WAITING) {
			fprintf(stderr, "[Main] handle_poll_events(): found waiting connection: [%d]\n", (connection)->socket);
			if (poll_fds[i].revents & POLLOUT) {
				fprintf(stderr, "[Main] handle_poll_events(): WAITING POLLOUT\n");
				if ((connection)->error) {
					fprintf(stderr, "[Main] handle_poll_events(): drop connection: [%d]\n", (connection)->socket);
					drop_connection(connections, poll_fds, connection, &i, sharedData);
					continue;
				} else {
					assert(false);
				}
			}
			if (poll_fds[i].revents & POLLIN) {
				fprintf(stderr, "[Main] handle_poll_events(): WAITING POLLIN\n");
				if ((connection)->bsize == 0) {
					if (((connection)->bsize = read((connection)->socket, (connection)->buffer, BUFFER_SIZE)) < 0) {
						fprintf(stderr, "[Main] handle_poll_events(): read from connection error: %s\n", strerror(errno));
						(connection)->error = SERVER_ERR;
						drop_connection(connections, poll_fds, connection, &i, sharedData);
						continue;
					}
					if ((connection)->bsize == 0) {
						fprintf(stderr, "[Main] handle_poll_events(): drop connection: [%d]\n", (connection)->socket);
						drop_connection(connections, poll_fds, connection, &i, sharedData);
						continue;
					}
					if (!(connection)->fullRequest) {
						(connection)->request = (char *) realloc((connection)->request,
																 (connection)->requestLength + (connection)->bsize);
						memcpy(&(connection)->request[(connection)->requestLength],
							   (connection)->buffer, (connection)->bsize);
						(connection)->requestLength += (connection)->bsize;
					}
					(connection)->bsize = 0; // just ignoring all messages from the client after a full request
				}
			}
		}
	}
}
