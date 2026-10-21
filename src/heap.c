/*
 * heap.c - a malloc-style dynamic memory allocator.
 *
 * Design
 * ------
 * The heap is a single contiguous region obtained from mmap(). Memory is
 * tracked with an *implicit free list*: every block begins with an 8-byte
 * header, and blocks sit back-to-back so the list is walked by stepping over
 * each block's size.
 *
 * Header layout (8 bytes, block sizes are multiples of 8 so the low 3 bits are
 * free to use as flags):
 *
 *     bit 0  - this block is allocated
 *     bit 1  - the *previous* block is allocated
 *     bits 3+ - block size in bytes (including the header)
 *
 * Allocated blocks carry only a header. Free blocks additionally carry an
 * 8-byte footer (a copy of the header) at their tail, so that when a block is
 * freed its left neighbour can be found in O(1) by reading the footer sitting
 * just before it ("boundary tags"). The "previous allocated" bit means we never
 * need a footer on allocated blocks.
 *
 * Placement is best-fit; blocks are split when the remainder is large enough,
 * and free() coalesces immediately with both neighbours.
 */

#define _GNU_SOURCE
#include "heap.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define WSIZE 8UL                 /* header / footer size in bytes */
#define ALIGNMENT 8UL             /* payload alignment */
#define MIN_BLOCK (2UL * WSIZE)   /* smallest block: header + footer */

#define ALLOC_BIT 0x1UL
#define PREV_ALLOC_BIT 0x2UL
#define SIZE_MASK (~0x7UL)

static char *g_heap;       /* base of the mmap region */
static size_t g_heap_size; /* total bytes mapped */

/* ---- low-level header/footer access -------------------------------------- */

static inline uint64_t get(const char *p) { return *(const uint64_t *)p; }
static inline void put(char *p, uint64_t v) { *(uint64_t *)p = v; }

static inline uint64_t pack(size_t size, int prev_alloc, int alloc) {
    return (uint64_t)size | (prev_alloc ? PREV_ALLOC_BIT : 0) | (alloc ? ALLOC_BIT : 0);
}

static inline size_t blk_size(const char *b) { return get(b) & SIZE_MASK; }
static inline int blk_alloc(const char *b) { return (int)(get(b) & ALLOC_BIT); }
static inline int blk_prev_alloc(const char *b) { return (int)((get(b) & PREV_ALLOC_BIT) >> 1); }

static inline char *next_blk(char *b) { return b + blk_size(b); }
/* Valid only when the previous block is free (its footer is the word before b). */
static inline char *prev_blk(char *b) { return b - (get(b - WSIZE) & SIZE_MASK); }

static inline void set_prev_alloc(char *b, int v) {
    uint64_t h = get(b);
    put(b, v ? (h | PREV_ALLOC_BIT) : (h & ~PREV_ALLOC_BIT));
}

/* Write a free block's header and matching footer. */
static inline void mark_free(char *b, size_t size, int prev_alloc) {
    put(b, pack(size, prev_alloc, 0));
    put(b + size - WSIZE, pack(size, prev_alloc, 0));
}

static inline size_t align_up(size_t n, size_t a) { return (n + a - 1) & ~(a - 1); }

/* The first real block sits just past the 8-byte prologue. */
static inline char *first_blk(void) { return g_heap + WSIZE; }

/* ---- heap lifecycle ------------------------------------------------------ */

int heap_init(size_t size) {
    if (g_heap) heap_destroy();

    /* Round up to a page so mmap is happy, and leave room for the prologue and
     * epilogue sentinels. */
    long pg = sysconf(_SC_PAGESIZE);
    size_t page = (pg > 0) ? (size_t)pg : 4096UL;
    size_t total = align_up(size + 2 * WSIZE, page);

    void *region = mmap(NULL, total, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) return -1;

    g_heap = region;
    g_heap_size = total;

    /* Prologue: an 8-byte allocated sentinel so the first real block always has
     * an allocated left neighbour. */
    put(g_heap, pack(WSIZE, 1, 1));

    /* One big free block spanning the middle. */
    size_t free_size = total - 2 * WSIZE;
    char *b = first_blk();
    mark_free(b, free_size, 1);

    /* Epilogue: a zero-size allocated sentinel marking the end of the heap. */
    char *epilogue = g_heap + total - WSIZE;
    put(epilogue, pack(0, 0, 1));
    return 0;
}

void heap_destroy(void) {
    if (g_heap) {
        munmap(g_heap, g_heap_size);
        g_heap = NULL;
        g_heap_size = 0;
    }
}

/* ---- allocation ---------------------------------------------------------- */

/* Best-fit search over the implicit list. */
static char *find_fit(size_t asize) {
    char *best = NULL;
    for (char *b = first_blk(); blk_size(b) > 0; b = next_blk(b)) {
        if (!blk_alloc(b) && blk_size(b) >= asize) {
            if (!best || blk_size(b) < blk_size(best)) best = b;
        }
    }
    return best;
}

/* Place an allocation of asize bytes in block b, splitting off the remainder as
 * a new free block when it is large enough. */
static void place(char *b, size_t asize) {
    size_t csize = blk_size(b);
    int prev_alloc = blk_prev_alloc(b);

    if (csize - asize >= MIN_BLOCK) {
        /* Allocate the front, leave the tail as a free block. */
        put(b, pack(asize, prev_alloc, 1));
        char *rem = b + asize;
        mark_free(rem, csize - asize, 1);
        set_prev_alloc(next_blk(rem), 0); /* block after the remainder */
    } else {
        /* Use the whole block. */
        put(b, pack(csize, prev_alloc, 1));
        set_prev_alloc(next_blk(b), 1);
    }
}

void *my_malloc(size_t size) {
    if (!g_heap || size == 0) return NULL;

    size_t asize = align_up(size + WSIZE, ALIGNMENT); /* room for header */
    if (asize < MIN_BLOCK) asize = MIN_BLOCK;

    char *b = find_fit(asize);
    if (!b) return NULL;
    place(b, asize);
    return b + WSIZE; /* payload */
}

/* ---- free + coalescing --------------------------------------------------- */

static char *coalesce(char *b) {
    size_t size = blk_size(b);
    int prev_alloc = blk_prev_alloc(b);
    char *next = b + size;
    int next_alloc = blk_alloc(next);

    if (prev_alloc && !next_alloc) {
        /* merge with the block on the right */
        size += blk_size(next);
    } else if (!prev_alloc && next_alloc) {
        /* merge with the block on the left */
        char *prev = prev_blk(b);
        prev_alloc = blk_prev_alloc(prev);
        size += blk_size(prev);
        b = prev;
    } else if (!prev_alloc && !next_alloc) {
        /* merge with both neighbours */
        char *prev = prev_blk(b);
        prev_alloc = blk_prev_alloc(prev);
        size += blk_size(prev) + blk_size(next);
        b = prev;
    }

    mark_free(b, size, prev_alloc);
    set_prev_alloc(b + size, 0); /* right neighbour now follows a free block */
    return b;
}

void my_free(void *ptr) {
    if (!ptr) return;
    char *b = (char *)ptr - WSIZE;
    size_t size = blk_size(b);
    int prev_alloc = blk_prev_alloc(b);

    mark_free(b, size, prev_alloc);
    set_prev_alloc(next_blk(b), 0);
    coalesce(b);
}

/* ---- debug --------------------------------------------------------------- */

void heap_dump(void) {
    if (!g_heap) {
        printf("(heap not initialised)\n");
        return;
    }
    printf("heap %p (%zu bytes):\n", (void *)g_heap, g_heap_size);
    for (char *b = first_blk(); blk_size(b) > 0; b = next_blk(b)) {
        printf("  [%p] size=%-7zu %s prev=%s\n", (void *)b, blk_size(b),
               blk_alloc(b) ? "ALLOC" : "FREE ",
               blk_prev_alloc(b) ? "A" : "F");
    }
    printf("  [epilogue]\n");
}
