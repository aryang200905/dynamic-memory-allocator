# Dynamic Memory Allocator

A `malloc`-style heap allocator written from scratch in C. It manages a single
region of memory obtained from the OS with an **implicit free list**, using
**best-fit** placement, **block splitting**, and **immediate boundary-tag
coalescing** to keep fragmentation down.

It is self-contained (it does not override the C library's `malloc`), so it can
be linked into a test program and exercised directly.

## Design

Every block starts with an 8-byte header. Because block sizes are multiples of 8,
the low 3 bits of the header are free for flags:

```
 63                                3   2   1   0
+------------------------------------+---+---+---+
|            block size              | — | P | A |
+------------------------------------+---+---+---+
   A = this block is allocated
   P = the previous block is allocated
```

- **Allocated blocks** carry only a header. The "previous allocated" (`P`) bit
  removes the need for a footer on allocated blocks.
- **Free blocks** additionally store an 8-byte **footer** (a copy of the header)
  at their tail. When a block is freed, its left neighbour's size can therefore
  be read in **O(1)** from the word just before it — classic boundary tags — so
  coalescing backward is constant-time.
- **Payloads are 8-byte aligned**, and the smallest block is 16 bytes
  (header + footer).

The heap is framed by an allocated **prologue** and a zero-size **epilogue**
sentinel so the first and last real blocks always have valid neighbours, which
removes special cases from the coalescing logic.

### Operations

- **`my_malloc(size)`** — rounds the request up (payload + header, 8-aligned),
  scans the implicit list for the **best fit**, and **splits** the chosen block
  when the leftover is at least one minimum block.
- **`my_free(ptr)`** — marks the block free and **coalesces immediately** with
  the previous and next blocks (both directions) using the `P` bit and the
  neighbour footers.

## Building and testing

Requires a C compiler (tested with `cc`/clang and GCC) on Linux or macOS.

```bash
make          # builds libheap.a and the test binary
make check    # runs the unit tests
```

The test suite covers alignment, read/write correctness, best-fit selection,
two-sided coalescing, block reuse, and clean failure on exhaustion.

## API

```c
int   heap_init(size_t size);   // acquire a heap region from the OS
void *my_malloc(size_t size);   // best-fit allocation, 8-byte aligned
void  my_free(void *ptr);       // free + immediate coalescing
void  heap_destroy(void);       // return the region to the OS
void  heap_dump(void);          // print the block list (debug)
```

## License

Released under the [MIT License](LICENSE).
