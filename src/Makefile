# This file is part of Open Chess Game Database Standard.
#
# Copyright (c) 2022 Nguyen Pham (github@nguyenpham)
# Copyright (c) 2022 Developers
#
# Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
# or copy at http://opensource.org/licenses/MIT)

# Note for Windows/MinGW builds: this Makefile targets Linux/macOS (-ldl is
# not available on MinGW). server.cpp (the -server task, via the vendored
# cpp-httplib in 3rdparty/httplib/) additionally needs -lws2_32 for sockets
# when linking on Windows (add it alongside -lpthread on that platform),
# and -D_WIN32_WINNT=0x0A00 (Windows 10) since cpp-httplib refuses to build
# against older Windows SDK targets.
all:
	g++ -std=c++17 -DNDEBUG -O3 -c *.cpp
	g++ -std=c++17 -DNDEBUG -O3 -c board/*.cpp
	g++ -std=c++17 -DNDEBUG -O3 -c 3rdparty/SQLiteCpp/*.cpp
	gcc -DNDEBUG -O3 -c 3rdparty/sqlite3/*.c
	g++ -o ocgdb *.o -lpthread -ldl

clean:
	rm -f ocgdb *.o ./board/*.o ./3rdparty/SQLiteCpp/*.o ./3rdparty/sqlite3/*.o
