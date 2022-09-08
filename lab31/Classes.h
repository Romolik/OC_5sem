#include <iostream>
#include <map>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

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


/* Pair - {Str host, Str URI} */
typedef std::pair<std::string, std::string> Pair;


class DataBlock {
 public:
  ~DataBlock() {
	  delete [] buffer;
  }
  char* buffer = nullptr;
  ssize_t bsize = 0;
  DataBlock *nextBlock = nullptr;
};

typedef struct _PageDescriptor {
  DataBlock* data = nullptr;
  DataBlock* endOfData = nullptr;
  int clientCounter = 0;
  bool useCache = true;
  ssize_t size = 0;
  int error = SUCCESS;
  bool downloading = true;
} PageDescriptor;


class ConnectionDescriptor {
 public:
  ~ConnectionDescriptor() {
	  if (socket != -1) {
		  if (shutdown(socket, SHUT_RDWR) < 0) {
			  perror("error shutdown socket");
		  }
		  if (close(socket) < 0) {
			  perror("error close socket");
		  }
	  }
	  delete cacheKey;
	  //  free(request);

  }
  int socket = -1;
  ConnectionDescriptor *worker = nullptr;
  bool useCache = true;
  bool fullRequest = false;
  Pair *cacheKey = nullptr;
  DataBlock *currentBlock = nullptr;
  SocketType type;
  char buffer[BUFFER_SIZE] = {0};
  int error = SUCCESS;
  ssize_t bsize = 0;
  char* request = NULL;
  ssize_t requestLength = 0;
  bool connected = true;
  ConnectionStatus status = WAITING;
};

typedef std::map<int, ConnectionDescriptor *> Connections;
typedef std::vector<struct pollfd> PollFDs;
typedef std::map<Pair, PageDescriptor> Cache;