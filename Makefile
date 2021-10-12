CC = gcc
.SUFFIXES: .c .o

all: runsim testsim

runsim: runsim.o license.o
	gcc -g -o runsim runsim.o license.o

testsim: testsim.o license.o
	gcc -g -o $@ testsim.o license.o

.c.o:
	$(CC) -c $<

clean:
	rm -f *.o runsim testsim