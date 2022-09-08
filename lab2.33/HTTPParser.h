#include "Classes.h"
#include <string.h>

long long int find_headers_length(char* buffer, int size);
int check_protocol(std::string& headers, char* buffer);
std::string find_host(std::string &headers);