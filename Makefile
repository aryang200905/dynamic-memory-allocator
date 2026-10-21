CC = cc
CFLAGS = -Wall -Wextra -Werror -O2 -g -std=c11

all: libheap.a test_heap

libheap.a: src/heap.o
	ar rcs $@ src/heap.o

src/heap.o: src/heap.c src/heap.h
	$(CC) $(CFLAGS) -c src/heap.c -o $@

test_heap: test/test_heap.c src/heap.o
	$(CC) $(CFLAGS) test/test_heap.c src/heap.o -o $@

check: test_heap
	./test_heap

clean:
	rm -f src/heap.o libheap.a test_heap
	rm -rf *.dSYM test/*.dSYM

.PHONY: all check clean
