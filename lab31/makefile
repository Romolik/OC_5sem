CppCompiler=g++

CppFLAGS=-lsocket -lnsl

all: main.o HTTPParser.o Connection.o
        $(CppCompiler) main.o HTTPParser.o Connection.o -o main $(CppFLAGS)

main.o: main.cpp
        $(CppCompiler) $(CppFLAGS) -c main.cpp

HTTPParser.o: HTTPParser.cpp
         $(CppCompiler) $(CppFLAGS) -c HTTPParser.cpp

Connection.o: Connection.cpp
        $(CppCompiler) $(CppFLAGS) -c Connection.cpp

clear:
        rm *.o