CC=g++
SIMDFLAGS=-mavx -mavx2 -mfma
MULTFLAG=-lpthread
CFLAGS=-Wall -pedantic -O3 -g -std=c++17
#MACFLAGS=-stdlib=libc++

all:
	mkdir -p obj
	g++ -o obj/stencil main.cpp stencil.cpp $(CFLAGS) $(MULTFLAG) $(SIMDFLAGS) $(MACFLAGS)
	gcc -o obj/datagen datagen.cpp $(CFLAGS)

clean:
	rm -rf obj
