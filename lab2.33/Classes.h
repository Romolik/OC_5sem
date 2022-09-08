#include <iostream>
#include <map>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <queue>


#define DROP (-1)
#define NOT_READY (-2)
#define FAILURE -1
#define SUCCESS 0


#define UNSUPPORTED_METH 1
#define UNSUPPORTED_METHOD "HTTP/1.0 400 Unsupported method\r\n\r\n"
#define SERVER_ERR 2
#define SERVER_ERROR "HTTP/1.0 500 Internal Server Error\r\n\r\n"
#define WRONG_VER 3
#define WRONG_VERSION "HTTP/1.0 400 Wrong version of protocol. Only HTTP/1.0 supports\r\n\r\n"
#define WRONG_REQ 4
#define WRONG_REQUEST "HTTP/1.0 404 Not Found\r\n\r\n"

#define BUFFER_SIZE 10000 /*SO_SNDLOWAT > 4000*/

enum SocketType {
  SERVER, CLIENT
};

enum ConnectionStatus {
  WAITING, CONNECTED
};

class RWLock {
 private:
  pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;
 public:
  int unlock() {
	  return pthread_rwlock_unlock(&lock);
  }

  int read_lock() {
	  return pthread_rwlock_rdlock(&lock);
  }

  int write_lock() {
	  return pthread_rwlock_wrlock(&lock);
  }
};

class Mutex {
 private:
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
 public:
  int unlock() {
	  return pthread_mutex_unlock(&mutex);
  }

  int lock() {
	  return pthread_mutex_lock(&mutex);
  }

  pthread_mutex_t *get_mutex() {
	  return &mutex;
  }
};

class Cond {
 private:
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
 public:
  int m_lock() {
	  return pthread_mutex_lock(&mutex);
  }

  int wait() {
	  return pthread_cond_wait(&cond, &mutex);
  }

  int wait(pthread_mutex_t *_mutex) {
	  return pthread_cond_wait(&cond, _mutex);
  }

  int signal() {
	  return pthread_cond_signal(&cond);
  }

  int broadcast() {
	  return pthread_cond_broadcast(&cond);
  }

  int m_unlock() {
	  return pthread_mutex_unlock(&mutex);
  }
};

typedef struct _DataBlock {
  char *buffer = nullptr;
  ssize_t bsize = 0;
  struct _DataBlock *nextBlock = nullptr;
} DataBlock;

typedef struct _PageDescriptor {
  DataBlock *data = nullptr;
  DataBlock *endOfData = nullptr;
  int clientCounter = 0;
  bool useCache = true;
  pthread_t thread;
  ssize_t size = 0;
  int error = SUCCESS;
  bool downloading = true;
  RWLock sync;
} PageDescriptor;

/* Pair - {Str host, Str URI} */
typedef std::pair<std::string, std::string> Pair;

/*  there and later: client - connection, connections with the client socket,
 *                   server - connection, connections with the server socket
 *  if using cache:
 *  for the client, who first sent the request, worker - the server, that provides the response & data
 *  for the server, worker - client, who first sent the request
 *  if not using cache:
 *  for the client, worker - server, which provides the response & data
 *  for the server, worker - client, who sent the request*/

typedef struct _ConnectionDescriptor {
  int socket;
  struct _ConnectionDescriptor *worker = nullptr;
  bool useCache = true;
  bool fullRequest = false;
  Pair *cacheKey = nullptr;
  DataBlock *currentBlock = nullptr;
  SocketType type;
  char buffer[BUFFER_SIZE] = {0};
  int error = SUCCESS;
  ssize_t bsize = 0;
  char *request = nullptr;
  ssize_t requestLength = 0;
  bool connected = true;
  ConnectionStatus status = WAITING;
} ConnectionDescriptor;

typedef std::map<int, ConnectionDescriptor *> Connections;
typedef std::vector<struct pollfd> PollFDs;
typedef std::map<Pair, PageDescriptor> Cache;
typedef std::pair<std::string, std::string> Pair;


class SharedData {
 public:
  volatile int thread_pool_size;
  std::map<pthread_t, int> threadToPipe;
  std::map<pthread_t, std::queue<ConnectionDescriptor *>> threadToQueue;
  pthread_barrier_t queue_barrier;
  Cond queueSync;
  Cache cache;
  RWLock cacheSync;
  int filedes[2];
};