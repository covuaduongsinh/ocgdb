# This file is part of Open Chess Game Database Standard.
#
# Copyright (c) 2022 Nguyen Pham (github@nguyenpham)
# Copyright (c) 2022 Developers
#
# Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
# or copy at http://opensource.org/licenses/MIT)

# server.cpp (the -server task, via the vendored cpp-httplib in
# 3rdparty/httplib/) needs socket support and a recent-enough Windows SDK
# target when building on Windows/MinGW: -lws2_32 (sockets) and
# -D_WIN32_WINNT=0x0A00 (Windows 10; cpp-httplib refuses to build against
# older targets). -ldl (dlopen, used on Linux/macOS) doesn't exist on
# MinGW, so it must be dropped there too, not just have -lws2_32 added.
#
# Detected two ways so this works whether make is invoked from an
# MSYS/MinGW shell (uname reports MINGW64_NT-...) or from plain
# cmd.exe/PowerShell running mingw32-make (no uname, but Windows always
# sets the %OS% environment variable).
ifeq ($(OS),Windows_NT)
    ON_WINDOWS := 1
else ifneq (,$(findstring MINGW,$(shell uname -s 2>/dev/null)))
    ON_WINDOWS := 1
endif

ifdef ON_WINDOWS
    EXTRA_CXXFLAGS := -D_WIN32_WINNT=0x0A00
    EXTRA_LIBS := -lws2_32
else
    EXTRA_LIBS := -ldl
endif

all:
	g++ -std=c++17 -DNDEBUG -O3 $(EXTRA_CXXFLAGS) -c *.cpp
	g++ -std=c++17 -DNDEBUG -O3 $(EXTRA_CXXFLAGS) -c board/*.cpp
	g++ -std=c++17 -DNDEBUG -O3 $(EXTRA_CXXFLAGS) -c 3rdparty/SQLiteCpp/*.cpp
	gcc -DNDEBUG -O3 -c 3rdparty/sqlite3/*.c
	g++ -o ocgdb *.o -lpthread $(EXTRA_LIBS)

clean:
	rm -f ocgdb *.o ./board/*.o ./3rdparty/SQLiteCpp/*.o ./3rdparty/sqlite3/*.o
