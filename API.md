# XArray API Reference

Detailed reference for every public function, macro, and type in the XArray library.

---

## Table of Contents

- [Types and Constants](#types-and-constants)
- [Entry Encoding](#entry-encoding)
- [Initialisation and Teardown](#initialisation-and-teardown)
- [Simple API](#simple-api)
  - [xa_load](#xa_load)
  - [xa_store](#xa_store)
  - [xa_erase](#xa_erase)
  - [xa_empty](#xa_empty)
  - [xa_destroy](#xa_destroy)
- [Mark API](#mark-api)
  - [xa_set_mark](#xa_set_mark)
  - [xa_clear_mark](#xa_clear_mark)
  - [xa_get_mark](#xa_get_mark)
- [Search API](#search-api)
  - [xa_find](#xa_find)
  - [xa_find_after](#xa_find_after)
- [Iteration Macros](#iteration-macros)
  - [xa_for_each](#xa_for_each)
  - [xa_for_each_marked](#xa_for_each_marked)
- [Cursor (Advanced) API](#cursor-advanced-api)
  - [XA_STATE](#xa_state)
  - [xas_set](#xas_set)
  - [xas_rewind](#xas_rewind)
  - [xas_set_err](#xas_set_err)
  - [xas_error](#xas_error)
  - [xas_retry](#xas_retry)
  - [xas_load](#xas_load)
  - [xas_store](#xas_store)
  - [xas_find](#xas_find)
  - [xas_find_marked](#xas_find_marked)
  - [xas_set_mark](#xas_set_mark)
  - [xas_clear_mark](#xas_clear_mark)
  - [xas_get_mark](#xas_get_mark)
- [Locking Helpers](#locking-helpers)
  - [xa_lock / xa_unlock](#xa_lock--xa_unlock)
  - [xa_rcu_lock / xa_rcu_unlock](#xa_rcu_lock--xa_rcu_unlock)
- [Value Entries](#value-entries)
  - [xa_mk_value](#xa_mk_value)
  - [xa_to_value](#xa_to_value)
  - [xa_is_value](#xa_is_value)
- [Build Configuration](#build-configuration)

---

## Types and Constants

### `struct xarray`

The root structure of an XArray. Contains the tree head pointer, flags for
mark bookkeeping, and an optional lock.

```c
struct xarray {
#ifdef XA_CONFIG_LOCK
    xa_lock_t   xa_lock;
#endif
    unsigned int xa_flags;
    void        *xa_head;
};
```

Users should treat this as opaque and only interact through the API functions.

### `struct xa_state`

Cursor structure for the advanced API. Holds the current walk position within
the tree. Instantiate on the stack with `XA_STATE()`.

### `xa_mark_t`

An unsigned integer identifying a mark. Three marks are available:

| Constant   | Value | Description         |
|------------|-------|---------------------|
| `XA_MARK_0` | 0   | First mark bitmap   |
| `XA_MARK_1` | 1   | Second mark bitmap  |
| `XA_MARK_2` | 2   | Third mark bitmap   |
| `XA_MARK_MAX` | 0xFF | Sentinel: match any entry (used internally by `xa_for_each`) |

`XA_MAX_MARKS` is defined as `3`. Mark values ≥ 3 are silently ignored by all
mark functions.

### Tree Constants

| Constant         | Value | Description                                      |
|------------------|-------|--------------------------------------------------|
| `XA_CHUNK_SHIFT` | 6     | Bits consumed per tree level                     |
| `XA_CHUNK_SIZE`  | 64    | Slots per internal node (2^6)                    |
| `XA_CHUNK_MASK`  | 63    | Mask for extracting the slot offset              |
| `XA_MAX_MARKS`   | 3     | Number of independent mark bitmaps               |

The tree supports the full 64-bit key space with at most
⌈64/6⌉ = 11 levels.

### Sentinel Values

| Constant         | Description                                              |
|------------------|----------------------------------------------------------|
| `XA_ZERO_ENTRY`  | Returned by `xa_store` on error. Also used as an internal placeholder. |
| `XA_RETRY_ENTRY` | Indicates a slot is being modified (RCU readers should retry). |

---

## Entry Encoding

The XArray can store two kinds of user entries:

1. **Pointer entries** — any aligned `void *` pointer (low 2 bits must be 0).
2. **Value entries** — small integers encoded as tagged pointers. See
   [Value Entries](#value-entries).

**Restrictions**: You must not store `NULL`, `XA_ZERO_ENTRY`,
`XA_RETRY_ENTRY`, or any value where `xa_is_internal()` is true. Storing
`NULL` is equivalent to erasing. Storing internal entries is rejected by
`xa_store` (returns `XA_ZERO_ENTRY`).

---

## Initialisation and Teardown

### `xa_init`

```c
void xa_init(struct xarray *xa);
```

Initialise an xarray with default settings (no flags). Must be called before
any other operation on the xarray.

**Parameters:**
- `xa` — Pointer to the xarray to initialise.

**Used together with:** Every other API in this library requires a prior call to
`xa_init` (or `xa_init_flags`). When the xarray is no longer needed, pair it
with a cleanup sequence: iterate with `xa_for_each` to free user entries, then
call `xa_destroy` to release internal nodes.

```c
/* Full lifecycle */
struct xarray xa;
xa_init(&xa);                           // 1. initialise

xa_store(&xa, 0, my_ptr, 0);            // 2. use the array
xa_set_mark(&xa, 0, XA_MARK_0);

uint64_t idx;
void *entry;
xa_for_each(&xa, idx, entry) {          // 3. drain user entries
    xa_erase(&xa, idx);
    free(entry);
}
xa_destroy(&xa);                        // 4. release internal nodes
```

### `xa_init_flags`

```c
void xa_init_flags(struct xarray *xa, unsigned int flags);
```

Initialise an xarray with caller-specified flags. The flags field is reserved
for internal bookkeeping (head-level mark bits); user-supplied flags occupy the
low bits.

**Parameters:**
- `xa` — Pointer to the xarray to initialise.
- `flags` — Initial flags value.

---

## Simple API

The simple API handles locking internally when `XA_CONFIG_LOCK` is enabled.
These functions are safe to call from any context.

### `xa_load`

```c
void *xa_load(struct xarray *xa, uint64_t index);
```

Look up the entry stored at `index`.

**Parameters:**
- `xa` — Xarray to search.
- `index` — The index to look up (0 to `UINT64_MAX`).

**Returns:**
- The entry stored at `index`, or `NULL` if no entry exists.
- Internal entries are filtered to `NULL`.

**Locking:**
- Acquires `xa_rcu_lock` / `xa_rcu_unlock` internally if `XA_CONFIG_RCU` is
  enabled.
- No write lock needed.

**Used together with:** `xa_load` is the read counterpart to `xa_store`. A
typical pattern is to store entries and later retrieve them by index. When
the entry might be a value entry (created with `xa_mk_value`), test with
`xa_is_value` before interpreting the result.

```c
xa_store(&xa, 5, xa_mk_value(100), 0);  // store a value entry

void *e = xa_load(&xa, 5);              // retrieve it
if (xa_is_value(e))
    printf("value = %lu\n", xa_to_value(e));
```

---

### `xa_store`

```c
void *xa_store(struct xarray *xa, uint64_t index, void *entry, uint64_t gfp);
```

Store `entry` at `index`. If an entry already exists at that index, it is
replaced and the old entry is returned. Marks on the entry are preserved
across overwrites and cleared on erase.

Storing `NULL` is equivalent to calling `xa_erase`.

**Parameters:**
- `xa` — Xarray to modify.
- `index` — Target index (0 to `UINT64_MAX`).
- `entry` — Entry to store. Must not be an internal entry (`xa_is_internal(entry)` must be false). `NULL` erases.
- `gfp` — Reserved for future use (pass `0`).

**Returns:**
- The previous entry at `index`, or `NULL` if the slot was empty.
- `XA_ZERO_ENTRY` on error (allocation failure or invalid entry). The xarray
  is unchanged.

**Locking:**
- Acquires `xa_lock` / `xa_unlock` internally if `XA_CONFIG_LOCK` is enabled.

**Mark behavior:**
- Overwriting an existing entry preserves all marks set on it.
- Storing `NULL` (erase) clears all marks.
- Storing into an empty slot starts with no marks.

**Used together with:** `xa_store` is typically paired with `xa_load` for
retrieval and `xa_erase` for removal. After storing, you can tag entries with
`xa_set_mark` and later iterate only tagged entries with `xa_for_each_marked`.
The store-mark-iterate trio is the most common multi-API workflow:

```c
/* Store → Mark → Iterate trio */
xa_store(&xa, 10, page_a, 0);
xa_store(&xa, 20, page_b, 0);

xa_set_mark(&xa, 10, XA_MARK_0);        // tag as dirty
xa_set_mark(&xa, 20, XA_MARK_0);

uint64_t idx;
void *entry;
xa_for_each_marked(&xa, idx, entry, XA_MARK_0) {
    flush(entry);                       // write back dirty pages
    xa_clear_mark(&xa, idx, XA_MARK_0); // clear dirty flag
}
```

Check the return value: `XA_ZERO_ENTRY` signals an error, and any non-NULL,
non-error return is the previous entry that the caller may need to free.

---

### `xa_erase`

```c
void *xa_erase(struct xarray *xa, uint64_t index);
```

Remove the entry at `index`. The tree automatically shrinks when internal
nodes become empty.

**Parameters:**
- `xa` — Xarray to modify.
- `index` — Index to erase.

**Returns:**
- The removed entry, or `NULL` if no entry existed at `index`.

**Locking:**
- Acquires `xa_lock` / `xa_unlock` internally if `XA_CONFIG_LOCK` is enabled.

**Used together with:** `xa_erase` is the inverse of `xa_store`. Use it to
remove individual entries. The returned pointer is the caller's responsibility
to free. In bulk-removal scenarios, combine `xa_for_each` with `xa_erase`
followed by `xa_destroy`:

```c
uint64_t idx;
void *entry;
xa_for_each(&xa, idx, entry) {
    xa_erase(&xa, idx);
    free(entry);
}
xa_destroy(&xa);  // free internal nodes
```

---

### `xa_empty`

```c
bool xa_empty(const struct xarray *xa);
```

Check whether the xarray contains any entries.

**Parameters:**
- `xa` — Xarray to test.

**Returns:**
- `true` if the xarray has no entries, `false` otherwise.

**Locking:**
- Lock-free. Uses a volatile load when `XA_CONFIG_RCU` is enabled.

**Used together with:** `xa_empty` is commonly used as a guard before
iterating or as a postcondition check after bulk erase:

```c
if (!xa_empty(&xa)) {
    uint64_t idx;
    void *entry;
    xa_for_each(&xa, idx, entry)
        process(entry);
}
```

---

### `xa_destroy`

```c
void xa_destroy(struct xarray *xa);
```

Free all internal nodes of the xarray. After this call, the xarray is empty.

**Important:** This does **not** free user entries. The caller must free or
detach all stored entries before calling `xa_destroy`, or accept that they
are leaked.

It is safe to call `xa_destroy` on an already-empty xarray (no-op). It is
also safe to call it multiple times.

**Parameters:**
- `xa` — Xarray to destroy.

**Locking:**
- Acquires `xa_lock` / `xa_unlock` internally if `XA_CONFIG_LOCK` is enabled.

**Used together with:** `xa_destroy` should always be the very last call in
the xarray lifecycle. Before calling it, drain all user entries with
`xa_for_each` + `xa_erase` (if you need to free them). After `xa_destroy`,
the xarray is empty and can be reused with `xa_init`.

```c
/* Safe teardown pattern */
uint64_t index;
void *entry;
xa_for_each(&xa, index, entry) {  // 1. free user entries
    xa_erase(&xa, index);
    free(entry);
}
xa_destroy(&xa);                  // 2. release internal nodes
```

---

## Mark API

Each entry can be independently tagged with up to 3 marks (`XA_MARK_0`,
`XA_MARK_1`, `XA_MARK_2`). Marks are propagated through the tree so that
marked iteration is efficient — only subtrees containing marked entries are
visited.

Mark behavior summary:

| Operation              | Effect on marks                      |
|------------------------|--------------------------------------|
| `xa_store` (new entry) | No marks set                         |
| `xa_store` (overwrite) | All existing marks preserved         |
| `xa_erase`             | All marks cleared                    |
| `xa_store(NULL)`       | Same as erase — all marks cleared    |

### `xa_set_mark`

```c
void xa_set_mark(struct xarray *xa, uint64_t index, xa_mark_t mark);
```

Set `mark` on the entry at `index`. Does nothing if there is no entry at
`index` or if `mark` ≥ `XA_MAX_MARKS`.

**Parameters:**
- `xa` — Xarray.
- `index` — Index of the entry to mark.
- `mark` — Mark to set (`XA_MARK_0`, `XA_MARK_1`, or `XA_MARK_2`).

**Locking:**
- Acquires `xa_lock` / `xa_unlock` internally.

**Used together with:** `xa_set_mark` is typically preceded by `xa_store`
(an entry must exist before it can be marked) and followed by
`xa_for_each_marked` to iterate only marked entries—or by `xa_find` /
`xa_find_after` with a specific mark to search for them. Call
`xa_clear_mark` to remove the tag after processing:

```c
xa_store(&xa, 7, ptr, 0);               // store entry
xa_set_mark(&xa, 7, XA_MARK_1);         // tag it

if (xa_get_mark(&xa, 7, XA_MARK_1))     // query the tag
    printf("marked!\n");

xa_clear_mark(&xa, 7, XA_MARK_1);       // remove the tag
```

---

### `xa_clear_mark`

```c
void xa_clear_mark(struct xarray *xa, uint64_t index, xa_mark_t mark);
```

Clear `mark` on the entry at `index`. Does nothing if there is no entry at
`index` or if `mark` ≥ `XA_MAX_MARKS`. Clearing a mark that is not set is a
safe no-op.

**Parameters:**
- `xa` — Xarray.
- `index` — Index of the entry to unmark.
- `mark` — Mark to clear.

**Locking:**
- Acquires `xa_lock` / `xa_unlock` internally.

**Used together with:** `xa_clear_mark` is the counterpart of `xa_set_mark`.
The most common pattern is set-process-clear inside `xa_for_each_marked`:

```c
uint64_t idx;
void *entry;
xa_for_each_marked(&xa, idx, entry, XA_MARK_0) {
    write_back(entry);                   // process dirty entry
    xa_clear_mark(&xa, idx, XA_MARK_0);  // mark as clean
}
```

---

### `xa_get_mark`

```c
bool xa_get_mark(struct xarray *xa, uint64_t index, xa_mark_t mark);
```

Test whether `mark` is set on the entry at `index`.

**Parameters:**
- `xa` — Xarray.
- `index` — Index to test.
- `mark` — Mark to test.

**Returns:**
- `true` if the entry exists and has `mark` set, `false` otherwise.
- Returns `false` for out-of-range marks (≥ `XA_MAX_MARKS`).
- Returns `false` for empty indices.

**Locking:**
- Acquires `xa_rcu_lock` / `xa_rcu_unlock` internally.

**Used together with:** `xa_get_mark` is a point query—use it to test a
mark on a single known index. For bulk queries, use `xa_for_each_marked`
or `xa_find` with a mark filter instead.

---

## Search API

### `xa_find`

```c
void *xa_find(struct xarray *xa, uint64_t *indexp, uint64_t max, xa_mark_t mark);
```

Find the first entry at or after `*indexp`, up to `max`.

When `mark` is `XA_MARK_MAX`, any entry matches (iterate all entries).
When `mark` is a specific mark value, only entries with that mark match.

**Parameters:**
- `xa` — Xarray to search.
- `indexp` — Pointer to the starting index. Updated to the index of the
  found entry on success.
- `max` — Maximum index to search (inclusive).
- `mark` — `XA_MARK_MAX` for all entries, or a specific mark to filter.

**Returns:**
- The first matching entry, or `NULL` if none found in `[*indexp, max]`.

**Locking:**
- Acquires `xa_rcu_lock` / `xa_rcu_unlock` internally.

**Used together with:** `xa_find` is typically paired with `xa_find_after`
to walk through entries. Use `xa_find` for the first result, then loop
with `xa_find_after`. Together they implement the same traversal as
`xa_for_each`, but allow the caller to control stepping:

```c
uint64_t idx = 0;
void *entry = xa_find(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
while (entry) {
    printf("[%lu] = %p\n", (unsigned long)idx, entry);
    entry = xa_find_after(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
}
```

To search for only marked entries, pass a specific mark instead of
`XA_MARK_MAX`—this mirrors what `xa_for_each_marked` does internally.

---

### `xa_find_after`

```c
void *xa_find_after(struct xarray *xa, uint64_t *indexp, uint64_t max,
                    xa_mark_t mark);
```

Find the next entry after `*indexp`, up to `max`. Equivalent to incrementing
`*indexp` and calling `xa_find`.

**Parameters:**
- `xa` — Xarray.
- `indexp` — Pointer to the current index. Search starts at `*indexp + 1`.
  Updated on success.
- `max` — Maximum index (inclusive).
- `mark` — `XA_MARK_MAX` or a specific mark.

**Returns:**
- The next matching entry, or `NULL` if none found.
- Returns `NULL` immediately if `*indexp >= max`.

**Used together with:** `xa_find_after` is the continuation of `xa_find`.
See the loop example above in `xa_find`. This pair replaces
`xa_for_each` when you need to break out of iteration, apply a filter,
or collect results into a separate container.

---

## Iteration Macros

### `xa_for_each`

```c
xa_for_each(xa, index, entry) { ... }
```

Iterate over every entry in the xarray in ascending index order.

**Parameters:**
- `xa` — Pointer to `struct xarray`.
- `index` — `uint64_t` variable, set to the current entry's index on each
  iteration.
- `entry` — `void *` variable, set to the current entry.

Only non-NULL, non-internal entries are yielded.

**Used together with:** `xa_for_each` is built on `xa_find` + `xa_find_after`
internally. It is the primary way to scan the entire array. Combine it with
`xa_erase` for bulk removal, or with `xa_set_mark` to tag entries during a
full scan:

```c
/* Scan all entries, mark those that need processing */
uint64_t index;
void *entry;
xa_for_each(&xa, index, entry) {
    if (needs_flush(entry))
        xa_set_mark(&xa, index, XA_MARK_0);
}

/* Then process only the marked subset */
xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
    flush(entry);
    xa_clear_mark(&xa, index, XA_MARK_0);
}
```

---

### `xa_for_each_marked`

```c
xa_for_each_marked(xa, index, entry, mark) { ... }
```

Iterate over entries that have `mark` set, in ascending index order.

**Parameters:**
- `xa` — Pointer to `struct xarray`.
- `index` — `uint64_t` loop variable.
- `entry` — `void *` loop variable.
- `mark` — Mark to filter on (`XA_MARK_0`, `XA_MARK_1`, or `XA_MARK_2`).

**Used together with:** `xa_for_each_marked` is the read side of the mark
workflow. The full cycle is:

1. `xa_store` — add entries.
2. `xa_set_mark` — tag entries that need attention.
3. `xa_for_each_marked` — iterate only tagged entries.
4. `xa_clear_mark` — untag after processing.

```c
uint64_t index;
void *entry;
xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
    process_dirty(index, entry);
    xa_clear_mark(&xa, index, XA_MARK_0);
}
```

---

## Cursor (Advanced) API

The cursor API provides direct tree access and is more efficient for batch
operations. When `XA_CONFIG_LOCK` is enabled, the caller must hold `xa_lock`
for write operations (`xas_store`, `xas_set_mark`, `xas_clear_mark`) and
`xa_rcu_lock` for read operations (`xas_load`, `xas_find`, `xas_get_mark`).

### `XA_STATE`

```c
XA_STATE(name, array, index);
```

Declare and initialise a cursor (`struct xa_state`) on the stack.

**Parameters:**
- `name` — Variable name for the cursor.
- `array` — Pointer to the `struct xarray`.
- `index` — Initial index for the cursor.

**Used together with:** `XA_STATE` is the entry point for every cursor
operation. After declaring the cursor, the typical write workflow is:

```c
/* Cursor write workflow */
xa_lock(&xa);
XA_STATE(xas, &xa, index);
void *old = xas_store(&xas, new_entry);  // store under lock
if (xas_error(&xas))
    handle_error(xas_error(&xas));
xa_unlock(&xa);
```

For reads:

```c
/* Cursor read workflow */
xa_rcu_lock();
XA_STATE(xas, &xa, index);
void *entry = xas_load(&xas);
if (entry && !xas_retry(&xas, entry))
    use(entry);
xa_rcu_unlock();
```

---

### `xas_set`

```c
void xas_set(struct xa_state *xas, uint64_t index);
```

Reset the cursor to a new index. Clears any error state and marks the cursor
for restart (the tree will be re-walked on the next operation).

**Parameters:**
- `xas` — Cursor to reset.
- `index` — New index.

**Used together with:** `xas_set` is used to reposition a cursor after
`xas_store` reports an error, or to walk multiple indices in a single
locked section:

```c
xa_lock(&xa);
XA_STATE(xas, &xa, 0);
xas_store(&xas, entry_a, 0);

xas_set(&xas, 100);              // reposition to index 100
xas_store(&xas, entry_b, 0);     // store at index 100
xa_unlock(&xa);
```

---

### `xas_rewind`

```c
void xas_rewind(struct xa_state *xas);
```

Reset the cursor to restart the walk from its current index without changing
the index.

---

### `xas_set_err`

```c
void xas_set_err(struct xa_state *xas, int err);
```

Put the cursor into an error state. Subsequent operations (`xas_find`,
`xas_find_marked`, `xas_get_mark`) will return `NULL` / `false` until the
error is cleared with `xas_set`.

**Parameters:**
- `xas` — Cursor.
- `err` — Error code (typically `-ENOMEM` or `-EINVAL`).

---

### `xas_error`

```c
int xas_error(const struct xa_state *xas);
```

Retrieve the error code from the cursor.

**Returns:**
- The error code if the cursor is in error state, `0` otherwise.

**Used together with:** Always check `xas_error` after `xas_store`. If
non-zero, the store failed (usually `-ENOMEM`). Use `xas_set` to clear
the error and retry:

```c
xa_lock(&xa);
XA_STATE(xas, &xa, idx);
xas_store(&xas, entry);
if (xas_error(&xas)) {
    int err = xas_error(&xas);
    xa_unlock(&xa);
    // handle err, e.g. free memory and retry
    return err;
}
xa_unlock(&xa);
```

---

### `xas_retry`

```c
bool xas_retry(struct xa_state *xas, const void *entry);
```

Check if `entry` is `XA_RETRY_ENTRY` and, if so, rewind the cursor. Used in
RCU read-side loops to handle concurrent modifications.

**Returns:**
- `true` if retry is needed (cursor has been rewound), `false` otherwise.

**Used together with:** `xas_retry` is used in RCU read-side loops with
`xas_load`. When a concurrent writer replaces a node, the reader sees
`XA_RETRY_ENTRY`. Call `xas_retry` to detect this and re-walk:

```c
xa_rcu_lock();
XA_STATE(xas, &xa, idx);
void *entry;
do {
    entry = xas_load(&xas);
} while (xas_retry(&xas, entry));
// entry is now stable
xa_rcu_unlock();
```

---

### `xas_load`

```c
void *xas_load(struct xa_state *xas);
```

Walk the tree and load the entry at the cursor's current index. After return,
the cursor is positioned at the node containing the entry.

**Returns:**
- The entry at `xas->xa_index`, or `NULL` if absent.

**Locking:**
- Caller must hold `xa_rcu_lock` (if RCU enabled).

**Used together with:** `xas_load` is often followed by a conditional
`xas_store` (load-then-modify pattern) or by `xas_set_mark` /
`xas_get_mark` to query/update marks while the cursor is positioned:

```c
/* Load-then-modify under lock */
xa_lock(&xa);
XA_STATE(xas, &xa, idx);
void *old = xas_load(&xas);
if (old && should_replace(old)) {
    xas_store(&xas, new_entry);
    free(old);
}
xa_unlock(&xa);
```

---

### `xas_store`

```c
void *xas_store(struct xa_state *xas, void *entry);
```

Store `entry` at the cursor's current index. Creates intermediate nodes as
needed. Storing `NULL` erases the entry and may trigger tree shrinkage.

The cursor's `xa_sibs` field controls multi-slot (sibling) entries: if
`xa_sibs` is non-zero, `xa_sibs + 1` consecutive slots starting at the
cursor's offset are occupied by the entry.

**Parameters:**
- `xas` — Cursor (must be positioned via `XA_STATE` or `xas_set`).
- `entry` — Entry to store, or `NULL` to erase.

**Returns:**
- The previous entry at the index, or `NULL`.
- Sets `xas_error` to `-ENOMEM` on allocation failure.
- Sets `xas_error` to `-EINVAL` if sibling span is invalid.

**Locking:**
- Caller must hold `xa_lock`.

**Mark behavior:**
- Overwriting preserves existing marks on the canonical slot.
- Erasing clears all marks.

**Used together with:** `xas_store` is the low-level write primitive. For
batch stores, declare one `XA_STATE`, acquire `xa_lock` once, and call
`xas_set` + `xas_store` in a loop to amortize lock overhead:

```c
xa_lock(&xa);
XA_STATE(xas, &xa, 0);
for (int i = 0; i < N; i++) {
    xas_set(&xas, keys[i]);
    xas_store(&xas, values[i]);
    if (xas_error(&xas))
        break;
}
xa_unlock(&xa);
```

For sibling (multi-slot) entries, set `xas.xa_sibs` before calling
`xas_store`:

```c
xa_lock(&xa);
XA_STATE(xas, &xa, base_index);
xas.xa_sibs = 3;                // occupy 4 consecutive slots
xas_store(&xas, huge_page);
xa_unlock(&xa);
```

---

### `xas_find`

```c
void *xas_find(struct xa_state *xas, uint64_t max);
```

Find the next non-NULL entry after the cursor's current position, up to `max`.
On the first call after `XA_STATE` or `xas_set`, starts from the cursor's
index. On subsequent calls, advances past the previous result.

**Parameters:**
- `xas` — Cursor.
- `max` — Maximum index (inclusive).

**Returns:**
- The next entry, or `NULL` if none found.
- `xas->xa_index` is updated to the found entry's index.
- Returns `NULL` if the cursor is in error state.

**Locking:**
- Caller must hold `xa_rcu_lock` (if RCU enabled).

**Used together with:** `xas_find` is the cursor-level equivalent of
`xa_find_after`. Use it in a loop to walk entries without releasing the
lock between steps:

```c
/* Collect all entries in [0, 1000] under one lock */
xa_rcu_lock();
XA_STATE(xas, &xa, 0);
void *entry;
while ((entry = xas_find(&xas, 1000)) != NULL) {
    collect(xas.xa_index, entry);
}
xa_rcu_unlock();
```

---

### `xas_find_marked`

```c
void *xas_find_marked(struct xa_state *xas, uint64_t max, xa_mark_t mark);
```

Find the next entry with `mark` set, after the cursor's current position.

**Parameters:**
- `xas` — Cursor.
- `max` — Maximum index (inclusive).
- `mark` — Mark to search for (`XA_MARK_0`, `XA_MARK_1`, or `XA_MARK_2`).

**Returns:**
- The next marked entry, or `NULL`.
- Returns `NULL` for invalid marks (≥ `XA_MAX_MARKS`) or error state.

**Locking:**
- Caller must hold `xa_rcu_lock` (if RCU enabled).

**Used together with:** `xas_find_marked` is the cursor counterpart of
`xa_for_each_marked`. Combine it with `xas_clear_mark` to process and
untag entries in one locked pass:

```c
xa_lock(&xa);
XA_STATE(xas, &xa, 0);
void *entry;
while ((entry = xas_find_marked(&xas, UINT64_MAX, XA_MARK_0)) != NULL) {
    flush(entry);
    xas_clear_mark(&xas, XA_MARK_0);
}
xa_unlock(&xa);
```

---

### `xas_set_mark`

```c
void xas_set_mark(struct xa_state *xas, xa_mark_t mark);
```

Set `mark` on the entry at the cursor's current position. The cursor must
have been positioned by a prior `xas_load` or `xas_store`. Propagates the
mark up to all ancestor nodes.

**Parameters:**
- `xas` — Cursor.
- `mark` — Mark to set.

Does nothing if the cursor is in an error/special state or `mark` ≥
`XA_MAX_MARKS`.

**Locking:**
- Caller must hold `xa_lock`.

**Used together with:** `xas_set_mark` requires the cursor to be positioned
first—call `xas_load` or `xas_store` before marking. The typical pattern
is load-then-mark:

```c
xa_lock(&xa);
XA_STATE(xas, &xa, idx);
void *entry = xas_load(&xas);    // position the cursor
if (entry)
    xas_set_mark(&xas, XA_MARK_1);  // tag it
xa_unlock(&xa);
```

---

### `xas_clear_mark`

```c
void xas_clear_mark(struct xa_state *xas, xa_mark_t mark);
```

Clear `mark` on the entry at the cursor's current position. If no other
slots in the same node have the mark, it is also cleared from ancestor nodes.

**Parameters:**
- `xas` — Cursor.
- `mark` — Mark to clear.

**Locking:**
- Caller must hold `xa_lock`.

**Used together with:** `xas_clear_mark` is the counterpart of
`xas_set_mark`. See the `xas_find_marked` example above for the typical
find-process-clear loop.

---

### `xas_get_mark`

```c
bool xas_get_mark(struct xa_state *xas, xa_mark_t mark);
```

Test whether `mark` is set at the cursor's current position.

**Returns:**
- `true` if the mark is set, `false` otherwise.
- Returns `false` for error/special cursor states or invalid marks.

**Locking:**
- Caller must hold `xa_rcu_lock` (if RCU enabled).

**Used together with:** `xas_get_mark` is the cursor-level read for marks.
Combine with `xas_load` to conditionally check marks at a positioned cursor
without modifying them:

```c
xa_rcu_lock();
XA_STATE(xas, &xa, idx);
void *entry = xas_load(&xas);
if (entry && xas_get_mark(&xas, XA_MARK_2))
    printf("entry at %lu has MARK_2\n", (unsigned long)idx);
xa_rcu_unlock();
```

---

## Locking Helpers

### `xa_lock` / `xa_unlock`

```c
void xa_lock(struct xarray *xa);
void xa_unlock(struct xarray *xa);
```

Acquire / release the xarray's internal lock. Required when using the cursor
API for write operations.

When `XA_CONFIG_LOCK` is not defined, these compile to no-ops.

**Used together with:** `xa_lock` / `xa_unlock` bracket all cursor write
operations: `xas_store`, `xas_set_mark`, `xas_clear_mark`. The simple API
(`xa_store`, `xa_erase`, etc.) acquires the lock automatically, so you only
need explicit locking when using the cursor API.

---

### `xa_rcu_lock` / `xa_rcu_unlock`

```c
void xa_rcu_lock(void);
void xa_rcu_unlock(void);
```

Enter / exit an RCU read-side critical section. Required when using the cursor
API for read operations under `XA_CONFIG_RCU`.

When `XA_CONFIG_RCU` is not defined, these compile to no-ops.

**Used together with:** `xa_rcu_lock` / `xa_rcu_unlock` bracket all cursor
read operations: `xas_load`, `xas_find`, `xas_find_marked`, `xas_get_mark`.
Inside the critical section, use `xas_retry` to handle concurrent writes.

---

## Value Entries

Value entries store small unsigned integers directly in a slot without heap
allocation. The integer is encoded by shifting left 2 bits and setting the
value flag.

### `xa_mk_value`

```c
void *xa_mk_value(unsigned long v);
```

Encode integer `v` as a value entry. The maximum storable value is
`UINTPTR_MAX >> 2` (on 64-bit systems: 2^62 - 1).

---

### `xa_to_value`

```c
unsigned long xa_to_value(void *entry);
```

Decode a value entry back to its integer. Only valid if `xa_is_value(entry)`
is true.

---

### `xa_is_value`

```c
bool xa_is_value(void *entry);
```

Test whether `entry` is a value entry (as opposed to a plain pointer).

**Used together with:** The three value entry functions form a trio that is
always used together:

1. `xa_mk_value(v)` — encode an integer into a storable entry.
2. `xa_store(&xa, idx, xa_mk_value(v), 0)` — store it.
3. On retrieval: `xa_is_value(e)` to check, `xa_to_value(e)` to decode.

This lets you store small integers (up to 2^62 - 1 on 64-bit) without
allocating memory. Value entries and pointer entries can be mixed in the
same xarray—use `xa_is_value` to distinguish them:

```c
xa_store(&xa, 0, xa_mk_value(42), 0);   // value entry
xa_store(&xa, 1, malloc(64), 0);         // pointer entry

uint64_t idx;
void *e;
xa_for_each(&xa, idx, e) {
    if (xa_is_value(e))
        printf("[%lu] value = %lu\n", idx, xa_to_value(e));
    else
        printf("[%lu] pointer = %p\n", idx, e);
}
```

---

## Build Configuration

All configuration is compile-time via preprocessor defines. Set them before
including any xarray header.

| Define              | Effect                                                |
|---------------------|-------------------------------------------------------|
| `XA_CONFIG_LOCK`    | Embed a lock in `struct xarray`; simple API acquires it automatically. |
| `XA_CONFIG_RCU`     | Enable RCU read-side sections and deferred node freeing. |
| `XA_CUSTOM_ALLOC`   | User provides `xa_alloc_fn` / `xa_free_fn` instead of malloc/free. |
| `XA_CUSTOM_LOCK`    | User provides lock type and operations (requires `XA_CONFIG_LOCK`). |
| `XA_CUSTOM_RCU`     | User provides RCU read-lock and call_rcu (requires `XA_CONFIG_RCU`). |
| `XA_CUSTOM_BARRIERS`| User provides `xa_smp_rmb` / `xa_smp_wmb` / `xa_smp_mb` macros. |

### Default implementations

| Feature    | Default                                                     |
|------------|-------------------------------------------------------------|
| Allocation | `malloc` + `memset(0)` / `free`                            |
| Locking    | `pthread_mutex_t`                                           |
| RCU        | Stub (immediate callback, no actual deferral)               |
| Barriers   | Architecture-detected: x86 compiler barriers, ARM64 `dmb`, RISC-V `fence`, fallback to `__atomic_thread_fence` |

### Memory allocation contract

`xa_alloc_fn(size)` must return a pointer to at least `size` bytes of
**zeroed memory**, or `NULL` on failure. `xa_free_fn(ptr)` frees memory
previously allocated by `xa_alloc_fn`. `xa_free_fn(NULL)` must be a safe
no-op.
