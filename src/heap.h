#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

/*
 * A small malloc-style heap allocator operating over a single region of memory
 * obtained from the OS. It is self-contained (it does not replace the C library
 * malloc) so it can be exercised and tested directly.
 */

/* Acquire a heap region of at least `size` bytes from the OS and initialise the
 * free list. Returns 0 on success, -1 on failure. Calling it again resets the
 * heap to a single empty block. */
int heap_init(size_t size);

/* Release the heap region back to the OS. */
void heap_destroy(void);

/* Allocate at least `size` bytes, 8-byte aligned, using best-fit placement.
 * Returns a pointer to the payload, or NULL if the request cannot be satisfied. */
void *my_malloc(size_t size);

/* Free a block previously returned by my_malloc, coalescing immediately with
 * any adjacent free blocks. Passing NULL is a no-op. */
void my_free(void *ptr);

/* Print the block list (header: size/alloc) for debugging. */
void heap_dump(void);

#endif /* HEAP_H */
