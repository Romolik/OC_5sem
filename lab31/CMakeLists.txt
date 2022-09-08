cmake_minimum_required(VERSION 3.17)
project(lab31)

set(CMAKE_CXX_STANDARD 20)

add_executable(lab31 main.cpp HTTPParser.h Classes.h HTTPParser.cpp Connection.h Connection.cpp Classes.h)