CC = gcc
CFLAGS = -O2 -Wall -I .

all: main cgi

main: main.c wrapper.o 
	$(CC) $(CFLAGS) -o main main.c wrapper.o 

wrapper.o: wrapper.c
	$(CC) $(CFLAGS) -c wrapper.c

cgi:
	(cd cgi-bin; make)

clean:
	rm -f *.o main *~
	(cd cgi-bin; make clean)
