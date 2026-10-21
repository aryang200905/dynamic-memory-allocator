/*
 * Unit tests for the heap allocator. Each check aborts via assert() on failure;
 * reaching the end prints "all tests passed".
 */
#include "../src/heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int is_aligned(void *p) { return ((uintptr_t)p & 0x7UL) == 0; }

/* Read a block's size from the header that sits 8 bytes before the payload. */
static size_t block_size(void *payload) {
    uint64_t h = *((uint64_t *)payload - 1);
    return (size_t)(h & ~0x7UL);
}

static void test_alignment(void) {
    assert(heap_init(4096) == 0);
    void *a = my_malloc(1);
    void *b = my_malloc(17);
    void *c = my_malloc(64);
    assert(a && b && c);
    assert(is_aligned(a) && is_aligned(b) && is_aligned(c));
    /* smallest block holds header + footer = 16 bytes */
    assert(block_size(a) >= 16);
    /* 17-byte request + 8 header, rounded to 8 -> 32-byte block */
    assert(block_size(b) == 32);
    heap_destroy();
    printf("  test_alignment: ok\n");
}

static void test_write_read(void) {
    assert(heap_init(8192) == 0);
    char *p = my_malloc(100);
    assert(p);
    memset(p, 0xAB, 100);
    for (int i = 0; i < 100; i++) assert((unsigned char)p[i] == 0xAB);
    heap_destroy();
    printf("  test_write_read: ok\n");
}

static void test_best_fit(void) {
    assert(heap_init(8192) == 0);
    /* Create three free holes of different sizes, then request one that fits the
     * smallest adequately: best-fit must pick the tightest. */
    void *big = my_malloc(512);
    void *gap1 = my_malloc(64);   /* -> will become a 72-ish byte hole */
    void *keep1 = my_malloc(16);
    void *gap2 = my_malloc(32);   /* -> smaller hole */
    void *keep2 = my_malloc(16);
    (void)big;
    my_free(gap1);
    my_free(gap2);
    /* Request 24 bytes: fits both holes; best-fit should reuse the smaller
     * (gap2) rather than the larger (gap1). */
    void *small = my_malloc(24);
    assert(small == gap2);
    (void)keep1; (void)keep2;
    heap_destroy();
    printf("  test_best_fit: ok\n");
}

static void test_coalesce(void) {
    assert(heap_init(8192) == 0);
    void *a = my_malloc(100);
    void *b = my_malloc(100);
    void *c = my_malloc(100);
    assert(a && b && c);

    /* Free the outer two, then the middle: it must coalesce in BOTH directions
     * into one large block big enough to satisfy a request larger than any
     * single original block. */
    my_free(a);
    my_free(c);
    my_free(b);

    void *big = my_malloc(300);
    assert(big == a); /* reuses the merged region starting at a's block */
    heap_destroy();
    printf("  test_coalesce: ok\n");
}

static void test_reuse_after_free(void) {
    assert(heap_init(4096) == 0);
    void *p = my_malloc(200);
    my_free(p);
    void *q = my_malloc(200);
    assert(q == p); /* freed block is reused */
    heap_destroy();
    printf("  test_reuse_after_free: ok\n");
}

static void test_exhaustion(void) {
    assert(heap_init(4096) == 0);
    /* Eventually allocation must fail cleanly (NULL), not crash. */
    int failed = 0;
    for (int i = 0; i < 100000; i++) {
        if (!my_malloc(64)) { failed = 1; break; }
    }
    assert(failed);
    heap_destroy();
    printf("  test_exhaustion: ok\n");
}

int main(void) {
    test_alignment();
    test_write_read();
    test_best_fit();
    test_coalesce();
    test_reuse_after_free();
    test_exhaustion();
    printf("all tests passed\n");
    return 0;
}
