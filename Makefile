CC=gcc
FLAGS=-Wall -Werror -c

all: wzip pzip

wzip: wzip.o
	$(CC) wzip.o -o wzip

pzip: pzip.o
	$(CC) -pthread pzip.o -o pzip

pzip.o: pzip.c
	$(CC) $(FLAGS) pzip.c

wzip.o: wzip.c
	$(CC) $(FLAGS) wzip.c

clean:
	rm -rf *.o *.out pzip wzip
