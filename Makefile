CC = gcc
.SUFFIXES: .c .o

all: runsim testsim

runsim: runsim.o license.o
	gcc -Wall -g -o runsim runsim.o license.o

testsim: testsim.o license.o
	gcc -Wall -g -o $@ testsim.o license.o

.c.o:
	$(CC) -g -c $<

clean:
	rm -f *.o runsim testsim