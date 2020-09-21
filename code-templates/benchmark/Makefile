flags = -std=c++20 -isystem /usr/local/include -L/usr/local/lib -lpthread -lbenchmark_main -lbenchmark
optflags = -flto -Ofast
files = main.cpp

all: gcc
.PHONY: all

gcc: $(files)
	g++ $(flags) $(optflags) $(files)

clang: $(files)
	clang++ $(flags) $(optflags) $(files)

gcc-noopt: $(files)
	g++ $(flags) $(files)

clang-noopt: $(files)
	clang++ $(flags) $(files)


