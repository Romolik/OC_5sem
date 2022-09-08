#include "HTTPParser.h"
#include <stdlib.h>
#include <cassert>

long long int find_headers_length(char* buffer, int size) {
	assert(buffer != nullptr);
	assert(size >= 0);

	std::string buf;
	buf.assign(buffer, size);
	int pos;
	if((pos = buf.find("Content-Length:")) == std::string::npos) {
		return FAILURE;
	}
	int lb = buf.find_first_not_of(' ', pos + 15);
	int le = buf.find_first_of(" \r\n", lb);
	if (lb == std::string::npos || le == std::string::npos) {
		return FAILURE;
	}
	assert(le > lb);
	try {
		std::string length = buf.substr(lb, le - lb);
		return strtoll(length.c_str(), (char**)NULL, 10);
	} catch (std::out_of_range &ex){
		fprintf(stderr, "\nfind_length(): caught exception: %s\n", ex.what());
		return FAILURE;
	}
}

int check_protocol(std::string& headers, char* buffer) {
	assert(nullptr != buffer);

	const long unsigned int pos = headers.find("HTTP/");
	if (pos == std::string::npos || buffer[pos + 5] != '1') {
		return WRONG_VER;
	}
	if (buffer[pos + 7] != '0') {
		buffer[pos + 7] = '0';
	}
	return SUCCESS;
}

std::string find_host(std::string &headers) {
	std::string host;
	const long unsigned int h = headers.find("Host:");
	if (h == std::string::npos) {
		fprintf(stderr, "\nfind_host(): Not found header \"Host:\"\n");
		return host;
	}
	const long unsigned int hb = headers.find_first_not_of(' ', h + 5);
	const long unsigned int he = headers.find_first_of(" \r\n", hb);
	if (hb == std::string::npos || he == std::string::npos) {
		fprintf(stderr, "\nfind_host(): Not found \"Host\"\n");
		return host;
	}
	try {
		host = headers.substr(hb, he - hb);
	} catch (std::out_of_range &ex) {
		fprintf(stderr, "\nfind_host(): caught exception: %s\n", ex.what());
		return host;
	}

	return host;
}
