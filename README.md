# XArray — Standalone Radix Tree Library

A portable, Linux-style radix tree (XArray) extracted from xv6 and adapted for
standalone use with standard C.

The XArray maps `uint64_t` indices to `void *` entries. Each internal node has
64 slots (6-bit fan-out). The tree grows and shrinks automatically. Entries can
be tagged with up to 3 independent marks for efficient filtered iteration.

## Repository layout

```
xarray/
├── CMakeLists.txt          Build system
├── include/
│   ├── xarray.h            Public API
│   ├── xarray_type.h       Type definitions (structs, macros, encoding)
│   └── xarray_config.h     Pluggable allocation, locking, and RCU
├── src/
│   └── xarray.c            Implementation
└── test/
    └── test_xarray.c       25 cmocka unit tests
```

## Quick start

```sh
# Prerequisites: cmake ≥ 3.10, a C11 compiler, libcmocka-dev (for tests)
mkdir build && cd build
cmake ..
make
ctest          # or: ./test_xarray
```

## CMake options

| Option            | Default | Description                                  |
|-------------------|---------|----------------------------------------------|
| `XA_ENABLE_LOCK`  | `OFF`   | Compile with internal locking (`XA_CONFIG_LOCK`) |
| `XA_ENABLE_RCU`   | `OFF`   | Compile with RCU support (`XA_CONFIG_RCU`)   |
| `XA_BUILD_TESTS`  | `ON`    | Build the cmocka test suite                  |
| `ENABLE_ASAN`     | `ON`    | Enable AddressSanitizer for tests            |

Example — build with locking:

```sh
cmake .. -DXA_ENABLE_LOCK=ON
```

## API overview

### Simple API

```c
#include "xarray.h"

struct xarray xa;
xa_init(&xa);

xa_store(&xa, 42, my_ptr, 0);           // store
void *p = xa_load(&xa, 42);             // load
void *old = xa_erase(&xa, 42);          // erase

xa_destroy(&xa);                        // free internal nodes (not user data)
```

### Marks

```c
xa_set_mark(&xa, index, XA_MARK_0);     // tag an entry
xa_clear_mark(&xa, index, XA_MARK_0);   // untag
bool marked = xa_get_mark(&xa, index, XA_MARK_0);
```

### Iteration

```c
uint64_t index;
void *entry;

// All entries
xa_for_each(&xa, index, entry) { ... }

// Only entries with XA_MARK_1
xa_for_each_marked(&xa, index, entry, XA_MARK_1) { ... }
```

### Value entries

Small integers can be stored directly without heap allocation:

```c
xa_store(&xa, 0, xa_mk_value(42), 0);
void *e = xa_load(&xa, 0);
if (xa_is_value(e))
    printf("%lu\n", (unsigned long)xa_to_value(e));  // 42
```

### Cursor API

For fine-grained control (must hold lock if `XA_CONFIG_LOCK` is enabled):

```c
XA_STATE(xas, &xa, index);
xa_lock(&xa);
void *old = xas_store(&xas, entry);
xa_unlock(&xa);
```

---

## Porting to your project

### 1. Copy the files

Copy the `include/` and `src/` directories into your project. You only need
three headers and one `.c` file.

### 2. Add to your build

If using CMake, add the library to your project:

```cmake
add_library(xarray path/to/src/xarray.c)
target_include_directories(xarray PUBLIC path/to/include)
```

If using a plain Makefile, compile `xarray.c` with `-Ipath/to/include`.

### 3. Configure features

All configuration is done via preprocessor defines. Set them before including
any xarray header, either in your build system (`-DXA_CONFIG_LOCK`) or in a
project-wide config header.

#### No locking, no RCU (default)

Nothing to do. Lock and RCU calls compile away to no-ops. Use this for
single-threaded applications or when you manage synchronisation externally.

#### Enable locking

Define `XA_CONFIG_LOCK`. The simple API (`xa_store`, `xa_erase`, `xa_set_mark`,
etc.) will acquire/release the lock automatically.

The default lock implementation uses `pthread_mutex_t`. To use your own lock:

```c
#define XA_CONFIG_LOCK
#define XA_CUSTOM_LOCK

// Provide these before including xarray headers:
typedef my_spinlock_t xa_lock_t;

#define XA_LOCK_INITIALIZER(name)  MY_LOCK_INIT

static inline void xa_lock_init(xa_lock_t *lock)  { my_lock_init(lock); }
static inline void xa_spin_lock(xa_lock_t *lock)   { my_lock_acquire(lock); }
static inline void xa_spin_unlock(xa_lock_t *lock) { my_lock_release(lock); }
```

#### Enable RCU

Define `XA_CONFIG_RCU`. Read-side operations (`xa_load`, `xa_find`) will be
wrapped in RCU sections, and freed nodes will be deferred via `xa_call_rcu`.
Slot loads/stores will use acquire/release atomics.

The default RCU stub calls the free callback immediately (safe for
single-threaded use). To use your own RCU:

```c
#define XA_CONFIG_RCU
#define XA_CUSTOM_RCU

// Provide these before including xarray headers:
extern void xa_rcu_read_lock(void);
extern void xa_rcu_read_unlock(void);

typedef void (*xa_rcu_callback_t)(void *);
extern void xa_call_rcu(xa_rcu_callback_t cb, void *data);
```

### 4. Custom memory allocation

By default, nodes are allocated with `malloc` and freed with `free`.
To use your own allocator (slab, pool, arena, etc.):

```c
#define XA_CUSTOM_ALLOC

// Implement in exactly one .c file:
void *xa_alloc_fn(size_t size)
{
    void *p = my_alloc(size);
    if (p) my_memzero(p, size);   // must return zeroed memory
    return p;
}

void xa_free_fn(void *ptr)
{
    my_free(ptr);
}
```

### 5. Porting to a kernel / bare-metal environment

A typical kernel port involves:

1. **Define all three custom macros** in a project header included before
   xarray headers:
   ```c
   #define XA_CUSTOM_ALLOC
   #define XA_CONFIG_LOCK
   #define XA_CUSTOM_LOCK
   #define XA_CONFIG_RCU
   #define XA_CUSTOM_RCU
   ```

2. **Implement the allocation interface** using your slab/page allocator.
   `xa_alloc_fn` must return zeroed memory of at least `size` bytes.

3. **Implement the lock interface** using your kernel's spinlock or mutex.

4. **Implement the RCU interface** using your kernel's RCU subsystem. Key
   requirement: `xa_call_rcu(cb, data)` must defer `cb(data)` until all
   current RCU readers have completed.

5. **Replace `<errno.h>`** if your environment lacks it. The implementation
   only uses `ENOMEM` (12) and `EINVAL` (22). You can define them manually:
   ```c
   #define ENOMEM 12
   #define EINVAL 22
   ```

6. **Replace `<string.h>`** — only `memset` is used (in the default
   `xa_alloc_fn`). If you define `XA_CUSTOM_ALLOC`, no `<string.h>` include
   is needed from the config header.

### 6. Data structure reference

```
struct xarray          — Root. Contains optional lock, flags, head pointer.
struct xa_node         — Internal node. 64 slots, 3 mark bitmaps.
struct xa_state        — Stack-allocated cursor for the advanced API.
```

The tree auto-grows when storing at large indices and auto-shrinks when nodes
become empty. Each level consumes 6 bits of the index, so a full 64-bit key
space requires at most 11 levels.

## Testing

The test suite uses [cmocka](https://cmocka.org/). Install it:

```sh
# Debian/Ubuntu
sudo apt install libcmocka-dev

# macOS
brew install cmocka
```

Tests cover: empty/single/bulk operations, sequential and random insertion,
overwrite, erase of missing keys, iteration order, mark set/clear/iterate,
cursor API, value entries, sparse and large indices, and a 1024-entry scale
test. All tests run under AddressSanitizer by default.

## License

Derived from the Linux kernel xarray and xv6. See upstream licenses.
