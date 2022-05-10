CC = gcc
CFLAGS = -Wall -pedantic -g

mytar: mytar.c given.h given.o
	$(CC) $(CFLAGS) -o mytar mytar.c
all: mytar
given.o: given.c
	$(CC) $(CFLAGS) -c given.c
test: mytar
	./mytar
clean:
