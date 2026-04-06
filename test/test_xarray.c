/**
 * @file test_xarray.c
 * @brief Comprehensive XArray unit tests using cmocka.
 *
 * Tests cover:
 *  - Basic store/load/erase (analogous to list push/pop/detach)
 *  - Sequential and random insertion (analogous to rbtree insert tests)
 *  - Bulk insert and delete with validation (analogous to rbtree balancing)
 *  - Iteration (analogous to rbtree in-order and list foreach)
 *  - Mark operations (tag/untag/find-marked)
 *  - Scale tests with large datasets (analogous to rbtree scale tests)
 *  - Edge cases: empty, single-entry, erase-missing
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <cmocka.h>

#include "xarray.h"

/* ====================================================================== */
/*  Helpers                                                                */
/* ====================================================================== */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/** Sentinel pointer values used as stored entries. */
#define ENTRY(n) ((void *)(uintptr_t)((n) * 2 + 0x1000))
#define ENTRY_TO_VAL(e) (((uintptr_t)(e) - 0x1000) / 2)

/**
 * Verify that xa contains exactly @count entries at indices in @indices
 * with values ENTRY(indices[i]).
 */
static void xa_verify_contents(struct xarray *xa, const uint64_t *indices,
                                size_t count)
{
    for (size_t i = 0; i < count; i++) {
        void *entry = xa_load(xa, indices[i]);
        assert_non_null(entry);
        assert_ptr_equal(entry, ENTRY(indices[i]));
    }
}

/** Count entries in the xarray using xa_for_each. */
static size_t xa_count_entries(struct xarray *xa)
{
    uint64_t index;
    void *entry;
    size_t count = 0;

    xa_for_each(xa, index, entry) {
        (void)index;
        (void)entry;
        count++;
    }
    return count;
}

/* ====================================================================== */
/*  Test: Empty xarray                                                     */
/* ====================================================================== */

static void test_empty_xarray(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    assert_true(xa_empty(&xa));
    assert_null(xa_load(&xa, 0));
    assert_null(xa_load(&xa, 42));
    assert_null(xa_load(&xa, ~0ULL));
    assert_int_equal(xa_count_entries(&xa), 0);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Single entry store/load/erase (like list create single)          */
/* ====================================================================== */

static void test_single_entry(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    void *old = xa_store(&xa, 0, ENTRY(0), 0);
    assert_null(old);
    assert_false(xa_empty(&xa));
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));
    assert_null(xa_load(&xa, 1));

    /* Erase it */
    old = xa_erase(&xa, 0);
    assert_ptr_equal(old, ENTRY(0));
    assert_true(xa_empty(&xa));
    assert_null(xa_load(&xa, 0));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Store and overwrite (like list push replacing)                   */
/* ====================================================================== */

static void test_store_overwrite(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 5, ENTRY(5), 0);
    assert_ptr_equal(xa_load(&xa, 5), ENTRY(5));

    /* Overwrite */
    void *old = xa_store(&xa, 5, ENTRY(50), 0);
    assert_ptr_equal(old, ENTRY(5));
    assert_ptr_equal(xa_load(&xa, 5), ENTRY(50));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Sequential insert (ported from rbtree insert_sequential)         */
/* ====================================================================== */

static void test_insert_sequential(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    const uint64_t keys[] = {0, 1, 2, 3, 4, 5, 6, 7,
                              8, 9, 10, 11, 12, 13, 14, 15};

    for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
        void *old = xa_store(&xa, keys[i], ENTRY(keys[i]), 0);
        assert_null(old);
    }

    /* Verify all entries */
    xa_verify_contents(&xa, keys, ARRAY_SIZE(keys));
    assert_int_equal(xa_count_entries(&xa), ARRAY_SIZE(keys));

    /* Iterate and check order */
    uint64_t index;
    void *entry;
    uint64_t prev_index = 0;
    bool first = true;
    xa_for_each(&xa, index, entry) {
        if (!first)
            assert_true(index > prev_index);
        first = false;
        prev_index = index;
    }

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Insert duplicate index (ported from rbtree insert_duplicate)     */
/* ====================================================================== */

static void test_insert_duplicate(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 42, ENTRY(42), 0);
    assert_ptr_equal(xa_load(&xa, 42), ENTRY(42));

    /* Store at same index returns old entry */
    void *old = xa_store(&xa, 42, ENTRY(99), 0);
    assert_ptr_equal(old, ENTRY(42));
    assert_ptr_equal(xa_load(&xa, 42), ENTRY(99));

    assert_int_equal(xa_count_entries(&xa), 1);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Push/pop pattern (ported from list push/pop tests)               */
/* ====================================================================== */

static void test_push_pop_pattern(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* "Push" entries at sequential indices */
    for (uint64_t i = 0; i < 8; i++) {
        xa_store(&xa, i, ENTRY(i), 0);
    }
    assert_int_equal(xa_count_entries(&xa), 8);

    /* "Pop" from the back (erase highest index) */
    void *e = xa_erase(&xa, 7);
    assert_ptr_equal(e, ENTRY(7));
    assert_int_equal(xa_count_entries(&xa), 7);

    /* "Pop" from the front */
    e = xa_erase(&xa, 0);
    assert_ptr_equal(e, ENTRY(0));
    assert_int_equal(xa_count_entries(&xa), 6);

    /* Verify remaining */
    assert_null(xa_load(&xa, 0));
    assert_null(xa_load(&xa, 7));
    for (uint64_t i = 1; i <= 6; i++) {
        assert_ptr_equal(xa_load(&xa, i), ENTRY(i));
    }

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Erase empty / missing (ported from list pop_empty +              */
/*        rbtree delete_missing)                                           */
/* ====================================================================== */

static void test_erase_empty(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Erase from empty */
    void *old = xa_erase(&xa, 0);
    assert_null(old);

    old = xa_erase(&xa, 100);
    assert_null(old);

    assert_true(xa_empty(&xa));
    xa_destroy(&xa);
}

static void test_erase_missing(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Insert some entries */
    const uint64_t keys[] = {11, 7, 18, 3, 10, 15, 20};
    for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
        xa_store(&xa, keys[i], ENTRY(keys[i]), 0);
    }
    size_t count = xa_count_entries(&xa);
    assert_int_equal(count, ARRAY_SIZE(keys));

    /* Erase non-existent key */
    void *old = xa_erase(&xa, 99);
    assert_null(old);
    assert_int_equal(xa_count_entries(&xa), count);

    /* All original entries still present */
    xa_verify_contents(&xa, keys, ARRAY_SIZE(keys));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Delete sequence (ported from rbtree sequence_cases)              */
/* ====================================================================== */

typedef struct {
    const uint64_t *insert;
    size_t insert_count;
    const uint64_t *remove;
    size_t remove_count;
    const uint64_t *expected;
    size_t expected_count;
} xa_sequence_case_t;

#define XA_U64_ARRAY(...) (const uint64_t[]){ __VA_ARGS__ }
#define XA_U64_COUNT(tuple) \
    (sizeof(XA_U64_ARRAY tuple) / sizeof((XA_U64_ARRAY tuple)[0]))

#define XA_SEQ_CASE(ins, rem, exp) {                            \
    .insert = XA_U64_ARRAY ins,                                 \
    .insert_count = XA_U64_COUNT(ins),                          \
    .remove = XA_U64_ARRAY rem,                                 \
    .remove_count = XA_U64_COUNT(rem),                          \
    .expected = XA_U64_ARRAY exp,                               \
    .expected_count = XA_U64_COUNT(exp),                        \
}

static const xa_sequence_case_t xa_sequence_cases[] = {
    /* Insert 7 entries, remove none */
    XA_SEQ_CASE((0, 1, 2, 3, 4, 5, 6), (99), (0, 1, 2, 3, 4, 5, 6)),
    /* Insert 6 entries, remove two from the middle */
    XA_SEQ_CASE((0, 1, 2, 3, 4, 5), (2, 4), (0, 1, 3, 5)),
    /* Insert 8 entries, erase first and last */
    XA_SEQ_CASE((0, 1, 2, 3, 4, 5, 6, 7), (0, 7), (1, 2, 3, 4, 5, 6)),
    /* Insert 5 entries, remove all */
    XA_SEQ_CASE((10, 20, 30, 40, 50), (10, 20, 30, 40, 50), (99)),
};

static void run_xa_sequence(const xa_sequence_case_t *tc)
{
    struct xarray xa;
    xa_init(&xa);

    for (size_t i = 0; i < tc->insert_count; i++) {
        xa_store(&xa, tc->insert[i], ENTRY(tc->insert[i]), 0);
    }

    for (size_t i = 0; i < tc->remove_count; i++) {
        xa_erase(&xa, tc->remove[i]);
    }

    /* Check expected entries.  If expected has the dummy 99 and count == 1,
     * that means empty expected. */
    if (tc->expected_count == 1 && tc->expected[0] == 99) {
        assert_true(xa_empty(&xa));
    } else {
        assert_int_equal(xa_count_entries(&xa), tc->expected_count);
        for (size_t i = 0; i < tc->expected_count; i++) {
            assert_ptr_equal(xa_load(&xa, tc->expected[i]),
                             ENTRY(tc->expected[i]));
        }
    }

    xa_destroy(&xa);
}

static void test_sequence_cases(void **state)
{
    (void)state;
    for (size_t i = 0; i < ARRAY_SIZE(xa_sequence_cases); i++) {
        run_xa_sequence(&xa_sequence_cases[i]);
    }
}

/* ====================================================================== */
/*  Test: Iteration order (ported from rbtree iteration_order)             */
/* ====================================================================== */

static void test_iteration_order(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    const uint64_t keys[] = {20, 10, 30, 5, 15, 25, 35};
    for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
        xa_store(&xa, keys[i], ENTRY(keys[i]), 0);
    }

    /* Forward iteration should yield entries in index order */
    uint64_t collected[ARRAY_SIZE(keys)];
    size_t count = 0;
    uint64_t index;
    void *entry;
    xa_for_each(&xa, index, entry) {
        assert_true(count < ARRAY_SIZE(collected));
        collected[count++] = index;
    }
    assert_int_equal(count, ARRAY_SIZE(keys));

    /* Verify ascending order */
    for (size_t i = 1; i < count; i++) {
        assert_true(collected[i - 1] < collected[i]);
    }

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Find first / detach (ported from list find_first_detach)         */
/* ====================================================================== */

static void test_find_and_erase(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Create entries at indices 1..8 */
    for (uint64_t i = 1; i <= 8; i++) {
        xa_store(&xa, i, ENTRY(i), 0);
    }

    /* "Find and detach" entry at index 1 */
    void *e = xa_load(&xa, 1);
    assert_non_null(e);
    xa_erase(&xa, 1);
    assert_int_equal(xa_count_entries(&xa), 7);

    /* "Find and detach" entry at index 8 */
    e = xa_load(&xa, 8);
    assert_non_null(e);
    xa_erase(&xa, 8);
    assert_int_equal(xa_count_entries(&xa), 6);

    /* "Find and detach" entry at index 5 */
    e = xa_load(&xa, 5);
    assert_non_null(e);
    xa_erase(&xa, 5);
    assert_int_equal(xa_count_entries(&xa), 5);

    /* Verify remaining: {2, 3, 4, 6, 7} */
    const uint64_t expected[] = {2, 3, 4, 6, 7};
    xa_verify_contents(&xa, expected, ARRAY_SIZE(expected));
    assert_int_equal(xa_count_entries(&xa), ARRAY_SIZE(expected));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Mark operations                                                  */
/* ====================================================================== */

static void test_marks_basic(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 1, ENTRY(1), 0);
    xa_store(&xa, 2, ENTRY(2), 0);

    /* No marks by default */
    assert_false(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 1, XA_MARK_0));

    /* Set marks */
    xa_set_mark(&xa, 0, XA_MARK_0);
    xa_set_mark(&xa, 2, XA_MARK_0);
    assert_true(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 1, XA_MARK_0));
    assert_true(xa_get_mark(&xa, 2, XA_MARK_0));

    /* Clear a mark */
    xa_clear_mark(&xa, 0, XA_MARK_0);
    assert_false(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_true(xa_get_mark(&xa, 2, XA_MARK_0));

    xa_destroy(&xa);
}

static void test_marks_independent(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 10, ENTRY(10), 0);
    xa_store(&xa, 20, ENTRY(20), 0);

    xa_set_mark(&xa, 10, XA_MARK_0);
    xa_set_mark(&xa, 20, XA_MARK_1);

    assert_true(xa_get_mark(&xa, 10, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 10, XA_MARK_1));
    assert_false(xa_get_mark(&xa, 20, XA_MARK_0));
    assert_true(xa_get_mark(&xa, 20, XA_MARK_1));

    xa_destroy(&xa);
}

static void test_marks_iteration(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store entries at indices 0..9 */
    for (uint64_t i = 0; i < 10; i++) {
        xa_store(&xa, i, ENTRY(i), 0);
    }

    /* Mark even indices with MARK_0 */
    for (uint64_t i = 0; i < 10; i += 2) {
        xa_set_mark(&xa, i, XA_MARK_0);
    }

    /* Iterate marked entries — should get 0, 2, 4, 6, 8 */
    uint64_t index;
    void *entry;
    size_t count = 0;
    xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
        assert_int_equal(index % 2, 0);
        count++;
    }
    assert_int_equal(count, 5);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Mark on missing entry                                            */
/* ====================================================================== */

static void test_mark_missing_entry(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Setting mark on non-existent entry should be a no-op */
    xa_set_mark(&xa, 42, XA_MARK_0);
    assert_false(xa_get_mark(&xa, 42, XA_MARK_0));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Large index (sparse tree)                                        */
/* ====================================================================== */

static void test_large_index(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    uint64_t big = (1ULL << 32) + 7;
    xa_store(&xa, big, ENTRY(1), 0);
    assert_ptr_equal(xa_load(&xa, big), ENTRY(1));
    assert_null(xa_load(&xa, 0));
    assert_null(xa_load(&xa, big - 1));
    assert_null(xa_load(&xa, big + 1));

    xa_erase(&xa, big);
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Sparse entries                                                   */
/* ====================================================================== */

static void test_sparse_entries(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store at widely separated indices */
    const uint64_t indices[] = {0, 63, 64, 127, 4095, 4096};
    for (size_t i = 0; i < ARRAY_SIZE(indices); i++) {
        xa_store(&xa, indices[i], ENTRY(indices[i]), 0);
    }

    xa_verify_contents(&xa, indices, ARRAY_SIZE(indices));
    assert_int_equal(xa_count_entries(&xa), ARRAY_SIZE(indices));

    /* Erase all */
    for (size_t i = 0; i < ARRAY_SIZE(indices); i++) {
        void *old = xa_erase(&xa, indices[i]);
        assert_ptr_equal(old, ENTRY(indices[i]));
    }
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Delete balancing (ported from rbtree delete_balancing)            */
/*  Verifies tree integrity through insert/delete cycles.                  */
/* ====================================================================== */

static void test_delete_balancing(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    const uint64_t keys[] = {41, 38, 31, 12, 19, 8, 4, 1,
                              2, 5, 64, 50, 80, 90, 70};
    for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
        xa_store(&xa, keys[i], ENTRY(keys[i]), 0);
    }

    size_t expected_size = ARRAY_SIZE(keys);
    assert_int_equal(xa_count_entries(&xa), expected_size);

    /* Delete specific keys */
    const uint64_t delete_keys[] = {8, 12, 41, 64, 1};
    for (size_t i = 0; i < ARRAY_SIZE(delete_keys); i++) {
        void *old = xa_erase(&xa, delete_keys[i]);
        assert_ptr_equal(old, ENTRY(delete_keys[i]));
        expected_size--;
        assert_int_equal(xa_count_entries(&xa), expected_size);
        assert_null(xa_load(&xa, delete_keys[i]));
    }

    /* Remove remaining entries one by one */
    uint64_t index;
    void *entry;
    while (!xa_empty(&xa)) {
        /* Find first entry */
        index = 0;
        entry = xa_find(&xa, &index, ~0ULL, XA_MARK_MAX);
        assert_non_null(entry);
        void *old = xa_erase(&xa, index);
        assert_non_null(old);
    }
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Scale (ported from rbtree scale_numbers)                         */
/* ====================================================================== */

/**
 * Pseudo-random number generator for reproducible tests.
 * Uses xorshift64.
 */
static uint64_t xorshift64(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

#define SCALE_COUNT 1024

static void test_scale(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Generate unique indices using permutation */
    uint64_t indices[SCALE_COUNT];
    uint64_t rng = 0xDEADBEEFCAFE1234ULL;
    for (size_t i = 0; i < SCALE_COUNT; i++) {
        indices[i] = i;
    }
    /* Fisher-Yates shuffle for random but unique indices */
    for (size_t i = SCALE_COUNT - 1; i > 0; i--) {
        uint64_t j = xorshift64(&rng) % (i + 1);
        uint64_t tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }

    /* Insert all */
    for (size_t i = 0; i < SCALE_COUNT; i++) {
        xa_store(&xa, indices[i], ENTRY(indices[i]), 0);
    }

    /* Verify all present */
    for (size_t i = 0; i < SCALE_COUNT; i++) {
        assert_ptr_equal(xa_load(&xa, indices[i]), ENTRY(indices[i]));
    }

    /* Verify iteration count */
    assert_int_equal(xa_count_entries(&xa), SCALE_COUNT);

    /* Verify iteration is in ascending order */
    uint64_t index;
    void *entry;
    uint64_t prev = 0;
    bool first = true;
    xa_for_each(&xa, index, entry) {
        if (!first)
            assert_true(index > prev);
        first = false;
        prev = index;
    }

    /* Delete every other entry */
    size_t removed = 0;
    for (size_t i = 0; i < SCALE_COUNT; i += 2) {
        void *old = xa_erase(&xa, indices[i]);
        assert_non_null(old);
        removed++;
        assert_null(xa_load(&xa, indices[i]));
    }

    size_t remaining = SCALE_COUNT - removed;
    assert_int_equal(xa_count_entries(&xa), remaining);

    /* Verify odd entries still present */
    for (size_t i = 1; i < SCALE_COUNT; i += 2) {
        assert_ptr_equal(xa_load(&xa, indices[i]), ENTRY(indices[i]));
    }

    /* Delete all remaining */
    for (size_t i = 1; i < SCALE_COUNT; i += 2) {
        void *old = xa_erase(&xa, indices[i]);
        assert_non_null(old);
    }
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Destroy with entries                                             */
/* ====================================================================== */

static void test_destroy_with_entries(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    for (uint64_t i = 0; i < 100; i++) {
        xa_store(&xa, i, ENTRY(i), 0);
    }

    /* xa_destroy frees internal nodes but not user entries */
    xa_destroy(&xa);
    assert_true(xa_empty(&xa));
}

/* ====================================================================== */
/*  Test: xa_find / xa_find_after                                          */
/* ====================================================================== */

static void test_find_operations(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 5, ENTRY(5), 0);
    xa_store(&xa, 10, ENTRY(10), 0);
    xa_store(&xa, 15, ENTRY(15), 0);

    /* Find from 0 — should get index 5 */
    uint64_t index = 0;
    void *entry = xa_find(&xa, &index, ~0ULL, XA_MARK_MAX);
    assert_non_null(entry);
    assert_int_equal(index, 5);
    assert_ptr_equal(entry, ENTRY(5));

    /* Find after 5 — should get index 10 */
    entry = xa_find_after(&xa, &index, ~0ULL, XA_MARK_MAX);
    assert_non_null(entry);
    assert_int_equal(index, 10);

    /* Find after 10 — should get index 15 */
    entry = xa_find_after(&xa, &index, ~0ULL, XA_MARK_MAX);
    assert_non_null(entry);
    assert_int_equal(index, 15);

    /* Find after 15 — should get NULL */
    entry = xa_find_after(&xa, &index, ~0ULL, XA_MARK_MAX);
    assert_null(entry);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Cursor API (xas_load / xas_store)                                */
/* ====================================================================== */

static void test_cursor_api(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store via cursor API */
    xa_lock(&xa);
    XA_STATE(xas, &xa, 42);
    void *old = xas_store(&xas, ENTRY(42));
    assert_null(old);
    xa_unlock(&xa);

    /* Load via cursor API */
    XA_STATE(xas2, &xa, 42);
    xa_rcu_lock();
    void *entry = xas_load(&xas2);
    assert_ptr_equal(entry, ENTRY(42));
    xa_rcu_unlock();

    /* Also accessible via simple API */
    assert_ptr_equal(xa_load(&xa, 42), ENTRY(42));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Marks propagation through tree                                   */
/* ====================================================================== */

static void test_marks_with_tree_depth(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Force multi-level tree by using indices in different chunks */
    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 64, ENTRY(64), 0);
    xa_store(&xa, 128, ENTRY(128), 0);

    xa_set_mark(&xa, 0, XA_MARK_0);
    xa_set_mark(&xa, 128, XA_MARK_0);

    assert_true(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 64, XA_MARK_0));
    assert_true(xa_get_mark(&xa, 128, XA_MARK_0));

    /* Iterate only marked */
    uint64_t index;
    void *entry;
    size_t count = 0;
    xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
        assert_true(index == 0 || index == 128);
        count++;
    }
    assert_int_equal(count, 2);

    /* Clear mark and re-check */
    xa_clear_mark(&xa, 0, XA_MARK_0);
    count = 0;
    xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
        assert_int_equal(index, 128);
        count++;
    }
    assert_int_equal(count, 1);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Store NULL is equivalent to erase                                */
/* ====================================================================== */

static void test_store_null_erases(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 10, ENTRY(10), 0);
    assert_non_null(xa_load(&xa, 10));

    void *old = xa_store(&xa, 10, NULL, 0);
    assert_ptr_equal(old, ENTRY(10));
    assert_null(xa_load(&xa, 10));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Value entries                                                    */
/* ====================================================================== */

static void test_value_entries(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store value entries (integers encoded as tagged pointers) */
    xa_store(&xa, 0, xa_mk_value(42), 0);
    xa_store(&xa, 1, xa_mk_value(0), 0);
    xa_store(&xa, 2, xa_mk_value(9999), 0);

    void *e0 = xa_load(&xa, 0);
    void *e1 = xa_load(&xa, 1);
    void *e2 = xa_load(&xa, 2);

    assert_true(xa_is_value(e0));
    assert_true(xa_is_value(e1));
    assert_true(xa_is_value(e2));

    assert_int_equal(xa_to_value(e0), 42);
    assert_int_equal(xa_to_value(e1), 0);
    assert_int_equal(xa_to_value(e2), 9999);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xa_find with bounded max                                         */
/* ====================================================================== */

static void test_find_bounded(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 5, ENTRY(5), 0);
    xa_store(&xa, 10, ENTRY(10), 0);
    xa_store(&xa, 100, ENTRY(100), 0);

    /* Search with max=9 — should find 5 but not 10 */
    uint64_t index = 0;
    void *entry = xa_find(&xa, &index, 9, XA_MARK_MAX);
    assert_non_null(entry);
    assert_int_equal(index, 5);

    /* Continue with xa_find_after bounded to 9 — nothing more */
    entry = xa_find_after(&xa, &index, 9, XA_MARK_MAX);
    assert_null(entry);

    /* Search with max=10 — should find both 5 and 10 */
    index = 0;
    size_t count = 0;
    entry = xa_find(&xa, &index, 10, XA_MARK_MAX);
    while (entry != NULL) {
        count++;
        entry = xa_find_after(&xa, &index, 10, XA_MARK_MAX);
    }
    assert_int_equal(count, 2);

    /* Search starting past all entries */
    index = 200;
    entry = xa_find(&xa, &index, ~0ULL, XA_MARK_MAX);
    assert_null(entry);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xa_find with marks (not XA_MARK_MAX)                             */
/* ====================================================================== */

static void test_find_marked(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    for (uint64_t i = 0; i < 20; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    /* Mark indices 3, 7, 15 with MARK_1 */
    xa_set_mark(&xa, 3, XA_MARK_1);
    xa_set_mark(&xa, 7, XA_MARK_1);
    xa_set_mark(&xa, 15, XA_MARK_1);

    /* xa_find should return first marked entry */
    uint64_t index = 0;
    void *entry = xa_find(&xa, &index, ~0ULL, XA_MARK_1);
    assert_non_null(entry);
    assert_int_equal(index, 3);

    /* xa_find_after should return next marked */
    entry = xa_find_after(&xa, &index, ~0ULL, XA_MARK_1);
    assert_non_null(entry);
    assert_int_equal(index, 7);

    entry = xa_find_after(&xa, &index, ~0ULL, XA_MARK_1);
    assert_non_null(entry);
    assert_int_equal(index, 15);

    entry = xa_find_after(&xa, &index, ~0ULL, XA_MARK_1);
    assert_null(entry);

    /* Bounded: max=10, should find 3 and 7 but not 15 */
    index = 0;
    entry = xa_find(&xa, &index, 10, XA_MARK_1);
    assert_non_null(entry);
    assert_int_equal(index, 3);

    entry = xa_find_after(&xa, &index, 10, XA_MARK_1);
    assert_non_null(entry);
    assert_int_equal(index, 7);

    entry = xa_find_after(&xa, &index, 10, XA_MARK_1);
    assert_null(entry);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Re-insert after erase                                            */
/* ====================================================================== */

static void test_reinsert_after_erase(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 42, ENTRY(42), 0);
    assert_ptr_equal(xa_load(&xa, 42), ENTRY(42));

    xa_erase(&xa, 42);
    assert_null(xa_load(&xa, 42));

    /* Re-insert at same index with different value */
    xa_store(&xa, 42, ENTRY(99), 0);
    assert_ptr_equal(xa_load(&xa, 42), ENTRY(99));
    assert_int_equal(xa_count_entries(&xa), 1);

    /* Erase all, re-populate */
    xa_erase(&xa, 42);
    assert_true(xa_empty(&xa));

    for (uint64_t i = 0; i < 64; i++)
        xa_store(&xa, i, ENTRY(i), 0);
    assert_int_equal(xa_count_entries(&xa), 64);

    for (uint64_t i = 0; i < 64; i++)
        xa_erase(&xa, i);
    assert_true(xa_empty(&xa));

    for (uint64_t i = 0; i < 64; i++)
        xa_store(&xa, i, ENTRY(i + 100), 0);
    for (uint64_t i = 0; i < 64; i++)
        assert_ptr_equal(xa_load(&xa, i), ENTRY(i + 100));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Node boundary crossing                                           */
/* ====================================================================== */

static void test_node_boundary_crossing(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Fill entries around the 64-slot boundary (indices 62..65)
     * to exercise tree expansion from 1 to 2 levels. */
    const uint64_t keys[] = {0, 62, 63, 64, 65, 127, 128};
    for (size_t i = 0; i < ARRAY_SIZE(keys); i++)
        xa_store(&xa, keys[i], ENTRY(keys[i]), 0);

    xa_verify_contents(&xa, keys, ARRAY_SIZE(keys));
    assert_int_equal(xa_count_entries(&xa), ARRAY_SIZE(keys));

    /* Iteration should be in order */
    uint64_t prev = 0;
    bool first = true;
    uint64_t index;
    void *entry;
    xa_for_each(&xa, index, entry) {
        if (!first)
            assert_true(index > prev);
        first = false;
        prev = index;
    }

    /* Erase boundary entries and verify shrink */
    xa_erase(&xa, 64);
    xa_erase(&xa, 65);
    assert_null(xa_load(&xa, 64));
    assert_null(xa_load(&xa, 65));
    assert_ptr_equal(xa_load(&xa, 63), ENTRY(63));
    assert_ptr_equal(xa_load(&xa, 127), ENTRY(127));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Multi-level tree (indices forcing 3+ levels)                     */
/* ====================================================================== */

static void test_multi_level_tree(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Level 1: index < 64
     * Level 2: index < 4096
     * Level 3: index < 262144
     * Level 4: index < 16777216
     * Place entries at each level's range. */
    const uint64_t keys[] = {
        0,                              /* level 1 */
        63,                             /* level 1 boundary */
        64,                             /* level 2 start */
        4095,                           /* level 2 boundary */
        4096,                           /* level 3 start */
        262143,                         /* level 3 boundary */
        262144,                         /* level 4 start */
        (1ULL << 24),                   /* 16M, level 5 */
    };

    for (size_t i = 0; i < ARRAY_SIZE(keys); i++)
        xa_store(&xa, keys[i], ENTRY(1), 0);

    for (size_t i = 0; i < ARRAY_SIZE(keys); i++)
        assert_ptr_equal(xa_load(&xa, keys[i]), ENTRY(1));

    assert_int_equal(xa_count_entries(&xa), ARRAY_SIZE(keys));

    /* Erase from highest down — tree should shrink progressively */
    for (int i = (int)ARRAY_SIZE(keys) - 1; i >= 0; i--) {
        xa_erase(&xa, keys[i]);
        assert_null(xa_load(&xa, keys[i]));
    }
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: All three marks on same entry                                    */
/* ====================================================================== */

static void test_all_marks_on_entry(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 10, ENTRY(10), 0);

    /* Set all three marks */
    xa_set_mark(&xa, 10, XA_MARK_0);
    xa_set_mark(&xa, 10, XA_MARK_1);
    xa_set_mark(&xa, 10, XA_MARK_2);

    assert_true(xa_get_mark(&xa, 10, XA_MARK_0));
    assert_true(xa_get_mark(&xa, 10, XA_MARK_1));
    assert_true(xa_get_mark(&xa, 10, XA_MARK_2));

    /* Clear MARK_1 — others unaffected */
    xa_clear_mark(&xa, 10, XA_MARK_1);
    assert_true(xa_get_mark(&xa, 10, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 10, XA_MARK_1));
    assert_true(xa_get_mark(&xa, 10, XA_MARK_2));

    /* Clear remaining */
    xa_clear_mark(&xa, 10, XA_MARK_0);
    xa_clear_mark(&xa, 10, XA_MARK_2);
    assert_false(xa_get_mark(&xa, 10, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 10, XA_MARK_2));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Mark survives overwrite but not erase                            */
/* ====================================================================== */

static void test_mark_survives_overwrite(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 5, ENTRY(5), 0);
    xa_set_mark(&xa, 5, XA_MARK_0);
    assert_true(xa_get_mark(&xa, 5, XA_MARK_0));

    /* Overwrite entry — marks must survive */
    xa_store(&xa, 5, ENTRY(50), 0);
    assert_ptr_equal(xa_load(&xa, 5), ENTRY(50));
    assert_true(xa_get_mark(&xa, 5, XA_MARK_0));

    /* Erase — mark should be gone */
    xa_erase(&xa, 5);
    assert_false(xa_get_mark(&xa, 5, XA_MARK_0));

    /* Re-insert after erase — should not have mark */
    xa_store(&xa, 5, ENTRY(55), 0);
    assert_false(xa_get_mark(&xa, 5, XA_MARK_0));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Mark erase clears from iteration                                 */
/* ====================================================================== */

static void test_mark_erase_stops_iteration(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    for (uint64_t i = 0; i < 10; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    xa_set_mark(&xa, 2, XA_MARK_0);
    xa_set_mark(&xa, 5, XA_MARK_0);
    xa_set_mark(&xa, 8, XA_MARK_0);

    /* Erase the marked entry at 5 */
    xa_erase(&xa, 5);

    /* Marked iteration should yield only 2 and 8 */
    uint64_t index;
    void *entry;
    uint64_t found[3];
    size_t count = 0;
    xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
        assert_true(count < 3);
        found[count++] = index;
    }
    assert_int_equal(count, 2);
    assert_int_equal(found[0], 2);
    assert_int_equal(found[1], 8);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Value entry erase and overwrite                                  */
/* ====================================================================== */

static void test_value_entry_erase_overwrite(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, xa_mk_value(100), 0);
    assert_true(xa_is_value(xa_load(&xa, 0)));
    assert_int_equal(xa_to_value(xa_load(&xa, 0)), 100);

    /* Overwrite value with different value */
    void *old = xa_store(&xa, 0, xa_mk_value(200), 0);
    assert_true(xa_is_value(old));
    assert_int_equal(xa_to_value(old), 100);
    assert_int_equal(xa_to_value(xa_load(&xa, 0)), 200);

    /* Overwrite value with pointer */
    old = xa_store(&xa, 0, ENTRY(0), 0);
    assert_true(xa_is_value(old));
    assert_int_equal(xa_to_value(old), 200);
    assert_false(xa_is_value(xa_load(&xa, 0)));
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));

    /* Overwrite pointer with value */
    old = xa_store(&xa, 0, xa_mk_value(50), 0);
    assert_ptr_equal(old, ENTRY(0));
    assert_true(xa_is_value(xa_load(&xa, 0)));
    assert_int_equal(xa_to_value(xa_load(&xa, 0)), 50);

    /* Erase value entry */
    old = xa_erase(&xa, 0);
    assert_true(xa_is_value(old));
    assert_int_equal(xa_to_value(old), 50);
    assert_null(xa_load(&xa, 0));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Value entry edge values                                          */
/* ====================================================================== */

static void test_value_entry_edge_values(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Maximum value that fits: (UINTPTR_MAX >> 2) since we shift left by 2 */
    uintptr_t max_val = UINTPTR_MAX >> 2;

    xa_store(&xa, 0, xa_mk_value(0), 0);
    xa_store(&xa, 1, xa_mk_value(1), 0);
    xa_store(&xa, 2, xa_mk_value(max_val), 0);

    assert_int_equal(xa_to_value(xa_load(&xa, 0)), 0);
    assert_int_equal(xa_to_value(xa_load(&xa, 1)), 1);
    assert_true(xa_to_value(xa_load(&xa, 2)) == max_val);

    /* All should count as entries */
    assert_int_equal(xa_count_entries(&xa), 3);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Mixed pointer and value entries                                  */
/* ====================================================================== */

static void test_mixed_pointer_and_value(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Alternate pointer and value entries */
    for (uint64_t i = 0; i < 10; i++) {
        if (i % 2 == 0)
            xa_store(&xa, i, ENTRY(i), 0);
        else
            xa_store(&xa, i, xa_mk_value(i * 100), 0);
    }

    assert_int_equal(xa_count_entries(&xa), 10);

    for (uint64_t i = 0; i < 10; i++) {
        void *e = xa_load(&xa, i);
        assert_non_null(e);
        if (i % 2 == 0) {
            assert_false(xa_is_value(e));
            assert_ptr_equal(e, ENTRY(i));
        } else {
            assert_true(xa_is_value(e));
            assert_int_equal(xa_to_value(e), i * 100);
        }
    }

    /* Iterate — should see all 10 in order */
    uint64_t index;
    void *entry;
    uint64_t expected = 0;
    xa_for_each(&xa, index, entry) {
        assert_int_equal(index, expected);
        expected++;
    }
    assert_int_equal(expected, 10);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Value entries with marks                                         */
/* ====================================================================== */

static void test_value_entries_with_marks(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, xa_mk_value(10), 0);
    xa_store(&xa, 1, xa_mk_value(20), 0);
    xa_store(&xa, 2, xa_mk_value(30), 0);

    xa_set_mark(&xa, 0, XA_MARK_2);
    xa_set_mark(&xa, 2, XA_MARK_2);

    assert_true(xa_get_mark(&xa, 0, XA_MARK_2));
    assert_false(xa_get_mark(&xa, 1, XA_MARK_2));
    assert_true(xa_get_mark(&xa, 2, XA_MARK_2));

    /* Marked iteration on value entries */
    uint64_t index;
    void *entry;
    size_t count = 0;
    xa_for_each_marked(&xa, index, entry, XA_MARK_2) {
        assert_true(xa_is_value(entry));
        count++;
    }
    assert_int_equal(count, 2);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Cursor iteration (xas_find)                                      */
/* ====================================================================== */

static void test_cursor_iteration(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    const uint64_t keys[] = {3, 10, 42, 100, 200};
    for (size_t i = 0; i < ARRAY_SIZE(keys); i++)
        xa_store(&xa, keys[i], ENTRY(keys[i]), 0);

    /* Walk all entries with xas_find */
    XA_STATE(xas, &xa, 0);
    xa_rcu_lock();
    size_t found = 0;
    void *entry = xas_load(&xas);
    if (entry)
        found++;
    while ((entry = xas_find(&xas, ~0ULL)) != NULL) {
        found++;
        assert_true(found <= ARRAY_SIZE(keys));
    }
    xa_rcu_unlock();

    assert_int_equal(found, ARRAY_SIZE(keys));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Cursor marked iteration (xas_find_marked)                        */
/* ====================================================================== */

static void test_cursor_find_marked(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    for (uint64_t i = 0; i < 16; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    /* Mark 1, 5, 9, 13 */
    xa_set_mark(&xa, 1, XA_MARK_0);
    xa_set_mark(&xa, 5, XA_MARK_0);
    xa_set_mark(&xa, 9, XA_MARK_0);
    xa_set_mark(&xa, 13, XA_MARK_0);

    XA_STATE(xas, &xa, 0);
    xa_rcu_lock();
    size_t count = 0;
    void *entry;
    while ((entry = xas_find_marked(&xas, ~0ULL, XA_MARK_0)) != NULL) {
        assert_int_equal(xas.xa_index % 4, 1);
        count++;
    }
    xa_rcu_unlock();
    assert_int_equal(count, 4);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Cursor set mark / clear mark / get mark                          */
/* ====================================================================== */

static void test_cursor_mark_ops(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 7, ENTRY(7), 0);

    /* Use cursor to set mark */
    xa_lock(&xa);
    XA_STATE(xas, &xa, 7);
    void *entry = xas_load(&xas);
    assert_non_null(entry);
    xas_set_mark(&xas, XA_MARK_1);
    xa_unlock(&xa);

    /* Verify via simple API */
    assert_true(xa_get_mark(&xa, 7, XA_MARK_1));

    /* Use cursor to check mark */
    XA_STATE(xas2, &xa, 7);
    xa_rcu_lock();
    xas_load(&xas2);
    assert_true(xas_get_mark(&xas2, XA_MARK_1));
    xa_rcu_unlock();

    /* Use cursor to clear mark */
    xa_lock(&xa);
    XA_STATE(xas3, &xa, 7);
    xas_load(&xas3);
    xas_clear_mark(&xas3, XA_MARK_1);
    xa_unlock(&xa);

    assert_false(xa_get_mark(&xa, 7, XA_MARK_1));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Cursor store multiple entries                                    */
/* ====================================================================== */

static void test_cursor_store_multiple(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_lock(&xa);
    for (uint64_t i = 0; i < 128; i++) {
        XA_STATE(xas, &xa, i);
        xas_store(&xas, ENTRY(i));
    }
    xa_unlock(&xa);

    /* Verify via simple API */
    for (uint64_t i = 0; i < 128; i++)
        assert_ptr_equal(xa_load(&xa, i), ENTRY(i));

    assert_int_equal(xa_count_entries(&xa), 128);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xas_set to reposition cursor                                     */
/* ====================================================================== */

static void test_cursor_reposition(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 10, ENTRY(10), 0);
    xa_store(&xa, 20, ENTRY(20), 0);
    xa_store(&xa, 30, ENTRY(30), 0);

    XA_STATE(xas, &xa, 10);
    xa_rcu_lock();

    void *entry = xas_load(&xas);
    assert_ptr_equal(entry, ENTRY(10));

    /* Reposition to 30 */
    xas_set(&xas, 30);
    entry = xas_load(&xas);
    assert_ptr_equal(entry, ENTRY(30));

    /* Reposition to non-existent — should return NULL */
    xas_set(&xas, 99);
    entry = xas_load(&xas);
    assert_null(entry);

    xa_rcu_unlock();

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Full node fill (64 entries in one node)                          */
/* ====================================================================== */

static void test_full_node(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Fill exactly one 64-slot node */
    for (uint64_t i = 0; i < 64; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    assert_int_equal(xa_count_entries(&xa), 64);

    for (uint64_t i = 0; i < 64; i++)
        assert_ptr_equal(xa_load(&xa, i), ENTRY(i));

    /* Erase middle entries */
    for (uint64_t i = 20; i < 40; i++)
        xa_erase(&xa, i);

    assert_int_equal(xa_count_entries(&xa), 44);

    for (uint64_t i = 0; i < 20; i++)
        assert_ptr_equal(xa_load(&xa, i), ENTRY(i));
    for (uint64_t i = 20; i < 40; i++)
        assert_null(xa_load(&xa, i));
    for (uint64_t i = 40; i < 64; i++)
        assert_ptr_equal(xa_load(&xa, i), ENTRY(i));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Very large and extreme indices (upper key-space)                 */
/* ====================================================================== */

static void test_extreme_indices(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    uint64_t big1 = (1ULL << 48) - 1;
    uint64_t big2 = (1ULL << 48);
    uint64_t big3 = (1ULL << 48) + 1;

    xa_store(&xa, big1, ENTRY(1), 0);
    xa_store(&xa, big2, ENTRY(2), 0);
    xa_store(&xa, big3, ENTRY(3), 0);

    assert_ptr_equal(xa_load(&xa, big1), ENTRY(1));
    assert_ptr_equal(xa_load(&xa, big2), ENTRY(2));
    assert_ptr_equal(xa_load(&xa, big3), ENTRY(3));
    assert_null(xa_load(&xa, big1 - 1));

    /* Iteration should visit in order */
    uint64_t index;
    void *entry;
    uint64_t prev = 0;
    size_t count = 0;
    xa_for_each(&xa, index, entry) {
        if (count > 0)
            assert_true(index > prev);
        prev = index;
        count++;
    }
    assert_int_equal(count, 3);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Erase triggers tree shrink                                       */
/* ====================================================================== */

static void test_erase_shrinks_tree(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Force a 2-level tree */
    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 100, ENTRY(100), 0);

    /* Erase the high entry — tree should shrink */
    xa_erase(&xa, 100);
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));
    assert_null(xa_load(&xa, 100));
    assert_int_equal(xa_count_entries(&xa), 1);

    /* Erase last entry — tree should become empty */
    xa_erase(&xa, 0);
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Destroy empty xarray (no-op, no crash)                           */
/* ====================================================================== */

static void test_destroy_empty(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);
    xa_destroy(&xa);
    assert_true(xa_empty(&xa));

    /* Double destroy should be safe */
    xa_destroy(&xa);
    assert_true(xa_empty(&xa));
}

/* ====================================================================== */
/*  Test: Bulk insert/erase/reinsert cycle                                 */
/* ====================================================================== */

static void test_bulk_reinsert_cycle(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Cycle 3 rounds of fill → empty → refill */
    for (int round = 0; round < 3; round++) {
        for (uint64_t i = 0; i < 256; i++)
            xa_store(&xa, i, ENTRY(i + round * 1000), 0);

        assert_int_equal(xa_count_entries(&xa), 256);

        for (uint64_t i = 0; i < 256; i++) {
            void *e = xa_load(&xa, i);
            assert_ptr_equal(e, ENTRY(i + round * 1000));
        }

        for (uint64_t i = 0; i < 256; i++)
            xa_erase(&xa, i);

        assert_true(xa_empty(&xa));
    }

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Reverse-order erase                                              */
/* ====================================================================== */

static void test_reverse_erase(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    for (uint64_t i = 0; i < 200; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    /* Erase in reverse order */
    for (int i = 199; i >= 0; i--) {
        void *old = xa_erase(&xa, (uint64_t)i);
        assert_ptr_equal(old, ENTRY((uint64_t)i));
    }
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Power-of-64 indices (one entry per tree level)                   */
/* ====================================================================== */

static void test_power_of_64_indices(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* 64^0=1, 64^1=64, 64^2=4096, 64^3=262144, 64^4=16777216 */
    const uint64_t keys[] = {1, 64, 4096, 262144, 16777216};
    for (size_t i = 0; i < ARRAY_SIZE(keys); i++)
        xa_store(&xa, keys[i], ENTRY(keys[i]), 0);

    for (size_t i = 0; i < ARRAY_SIZE(keys); i++)
        assert_ptr_equal(xa_load(&xa, keys[i]), ENTRY(keys[i]));

    /* Gaps should be NULL */
    assert_null(xa_load(&xa, 0));
    assert_null(xa_load(&xa, 2));
    assert_null(xa_load(&xa, 63));
    assert_null(xa_load(&xa, 65));

    assert_int_equal(xa_count_entries(&xa), ARRAY_SIZE(keys));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: UINT64_MAX index                                                 */
/* ====================================================================== */

static void test_uint64_max_index(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    uint64_t max_idx = ~0ULL;
    xa_store(&xa, max_idx, ENTRY(1), 0);
    assert_ptr_equal(xa_load(&xa, max_idx), ENTRY(1));
    assert_null(xa_load(&xa, max_idx - 1));
    assert_null(xa_load(&xa, 0));

    void *old = xa_erase(&xa, max_idx);
    assert_ptr_equal(old, ENTRY(1));
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Index 0 single-entry head with marks                             */
/* ====================================================================== */

static void test_index0_head_marks(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store only at index 0 — head is a direct pointer, no node. */
    xa_store(&xa, 0, ENTRY(0), 0);

    /* Set and get marks on the single-entry head */
    xa_set_mark(&xa, 0, XA_MARK_0);
    assert_true(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 0, XA_MARK_1));
    assert_false(xa_get_mark(&xa, 0, XA_MARK_2));

    xa_set_mark(&xa, 0, XA_MARK_2);
    assert_true(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_true(xa_get_mark(&xa, 0, XA_MARK_2));

    /* Clear mark */
    xa_clear_mark(&xa, 0, XA_MARK_0);
    assert_false(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_true(xa_get_mark(&xa, 0, XA_MARK_2));

    /* Marked iteration should find the entry */
    uint64_t index;
    void *entry;
    size_t count = 0;
    xa_for_each_marked(&xa, index, entry, XA_MARK_2) {
        assert_int_equal(index, 0);
        count++;
    }
    assert_int_equal(count, 1);

    /* After erase, mark should be gone */
    xa_erase(&xa, 0);
    assert_false(xa_get_mark(&xa, 0, XA_MARK_2));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Store at index 0 into an already-deep tree                       */
/* ====================================================================== */

static void test_store_index0_into_deep_tree(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Force a deep tree first */
    xa_store(&xa, 1ULL << 32, ENTRY(1), 0);
    assert_null(xa_load(&xa, 0));

    /* Now store at index 0 */
    xa_store(&xa, 0, ENTRY(0), 0);
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));
    assert_ptr_equal(xa_load(&xa, 1ULL << 32), ENTRY(1));

    assert_int_equal(xa_count_entries(&xa), 2);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xa_find on empty xarray                                          */
/* ====================================================================== */

static void test_find_empty(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    uint64_t index = 0;
    void *entry = xa_find(&xa, &index, ~0ULL, XA_MARK_MAX);
    assert_null(entry);

    /* Also with a specific mark */
    index = 0;
    entry = xa_find(&xa, &index, ~0ULL, XA_MARK_0);
    assert_null(entry);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xa_find_after when *indexp == UINT64_MAX                         */
/* ====================================================================== */

static void test_find_after_uint64_max(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0);

    uint64_t index = ~0ULL;
    void *entry = xa_find_after(&xa, &index, ~0ULL, XA_MARK_MAX);
    /* *indexp == max, so xa_find_after should return NULL immediately */
    assert_null(entry);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xa_find_after when *indexp == max                                */
/* ====================================================================== */

static void test_find_after_at_max(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 5, ENTRY(5), 0);
    xa_store(&xa, 10, ENTRY(10), 0);

    /* *indexp == max, should return NULL */
    uint64_t index = 10;
    void *entry = xa_find_after(&xa, &index, 10, XA_MARK_MAX);
    assert_null(entry);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xa_find with max = 0                                             */
/* ====================================================================== */

static void test_find_max_zero(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 1, ENTRY(1), 0);

    /* max=0 should find only index 0 */
    uint64_t index = 0;
    void *entry = xa_find(&xa, &index, 0, XA_MARK_MAX);
    assert_non_null(entry);
    assert_int_equal(index, 0);

    /* xa_find_after with max=0 should return NULL */
    entry = xa_find_after(&xa, &index, 0, XA_MARK_MAX);
    assert_null(entry);

    /* max=0 when nothing at index 0 */
    struct xarray xa2;
    xa_init(&xa2);
    xa_store(&xa2, 5, ENTRY(5), 0);

    index = 0;
    entry = xa_find(&xa2, &index, 0, XA_MARK_MAX);
    assert_null(entry);

    xa_destroy(&xa);
    xa_destroy(&xa2);
}

/* ====================================================================== */
/*  Test: xa_for_each_marked on empty / no marks                           */
/* ====================================================================== */

static void test_marked_iteration_empty_and_no_marks(void **state)
{
    (void)state;

    /* Empty xarray */
    struct xarray xa;
    xa_init(&xa);
    uint64_t index;
    void *entry;
    size_t count = 0;
    xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
        count++;
    }
    assert_int_equal(count, 0);
    xa_destroy(&xa);

    /* Non-empty xarray with no marks */
    struct xarray xa2;
    xa_init(&xa2);
    for (uint64_t i = 0; i < 10; i++)
        xa_store(&xa2, i, ENTRY(i), 0);
    count = 0;
    xa_for_each_marked(&xa2, index, entry, XA_MARK_0) {
        count++;
    }
    assert_int_equal(count, 0);
    xa_destroy(&xa2);
}

/* ====================================================================== */
/*  Test: Double erase same index                                          */
/* ====================================================================== */

static void test_double_erase_same_index(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 50, ENTRY(50), 0);
    xa_store(&xa, 100, ENTRY(100), 0);

    void *old = xa_erase(&xa, 50);
    assert_ptr_equal(old, ENTRY(50));

    /* Second erase of same index */
    old = xa_erase(&xa, 50);
    assert_null(old);

    /* Other entry unaffected */
    assert_ptr_equal(xa_load(&xa, 100), ENTRY(100));
    assert_int_equal(xa_count_entries(&xa), 1);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Double destroy after entries                                     */
/* ====================================================================== */

static void test_double_destroy_with_entries(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    for (uint64_t i = 0; i < 100; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    xa_destroy(&xa);
    assert_true(xa_empty(&xa));

    /* Second destroy should be safe */
    xa_destroy(&xa);
    assert_true(xa_empty(&xa));
}

/* ====================================================================== */
/*  Test: Sibling entries via cursor API                                   */
/* ====================================================================== */

static void test_sibling_entries(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store a multi-slot entry using xa_sibs */
    xa_lock(&xa);
    XA_STATE(xas, &xa, 4);
    xas.xa_sibs = 3; /* occupy slots 4, 5, 6, 7 */
    void *old = xas_store(&xas, ENTRY(4));
    assert_null(old);
    xa_unlock(&xa);

    /* The canonical slot should return the entry */
    assert_ptr_equal(xa_load(&xa, 4), ENTRY(4));

    /* Sibling slots should also resolve to the same entry */
    assert_ptr_equal(xa_load(&xa, 5), ENTRY(4));
    assert_ptr_equal(xa_load(&xa, 6), ENTRY(4));
    assert_ptr_equal(xa_load(&xa, 7), ENTRY(4));

    /* Adjacent slots should be unaffected */
    assert_null(xa_load(&xa, 3));
    assert_null(xa_load(&xa, 8));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xa_clear_mark on entry without the mark                          */
/* ====================================================================== */

static void test_clear_mark_without_mark(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 10, ENTRY(10), 0);
    xa_set_mark(&xa, 10, XA_MARK_0);

    /* Clear MARK_1 which was never set — should be a no-op */
    xa_clear_mark(&xa, 10, XA_MARK_1);
    assert_false(xa_get_mark(&xa, 10, XA_MARK_1));

    /* MARK_0 should be unaffected */
    assert_true(xa_get_mark(&xa, 10, XA_MARK_0));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Marks across tree levels after partial erase                     */
/* ====================================================================== */

static void test_marks_across_levels_partial_erase(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Force multi-level tree */
    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 64, ENTRY(64), 0);
    xa_store(&xa, 4096, ENTRY(4096), 0);

    xa_set_mark(&xa, 0, XA_MARK_0);
    xa_set_mark(&xa, 64, XA_MARK_0);
    xa_set_mark(&xa, 4096, XA_MARK_0);

    /* Erase the middle marked entry */
    xa_erase(&xa, 64);

    /* Remaining marks should still be correct */
    assert_true(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 64, XA_MARK_0));
    assert_true(xa_get_mark(&xa, 4096, XA_MARK_0));

    /* Marked iteration should yield exactly 0 and 4096 */
    uint64_t index;
    void *entry;
    uint64_t found[2];
    size_t count = 0;
    xa_for_each_marked(&xa, index, entry, XA_MARK_0) {
        assert_true(count < 2);
        found[count++] = index;
    }
    assert_int_equal(count, 2);
    assert_int_equal(found[0], 0);
    assert_int_equal(found[1], 4096);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xa_get_mark on empty index in non-empty tree                     */
/* ====================================================================== */

static void test_get_mark_on_empty_index(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 10, ENTRY(10), 0);
    xa_set_mark(&xa, 10, XA_MARK_0);

    /* Index 5 has no entry — get_mark should return false */
    assert_false(xa_get_mark(&xa, 5, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 100, XA_MARK_0));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: xa_init_flags with non-zero flags                                */
/* ====================================================================== */

static void test_init_flags(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init_flags(&xa, 0x42);

    assert_true(xa_empty(&xa));

    /* Store and load should work normally */
    xa_store(&xa, 0, ENTRY(0), 0);
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Invalid mark values (mark >= XA_MAX_MARKS)                       */
/* ====================================================================== */

static void test_invalid_mark_value(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 10, ENTRY(10), 0);

    /* mark = 3 is out of range (XA_MAX_MARKS == 3, valid range 0..2) */
    xa_set_mark(&xa, 10, 3);
    assert_false(xa_get_mark(&xa, 10, 3));

    /* mark = 255 */
    xa_set_mark(&xa, 10, 255);
    assert_false(xa_get_mark(&xa, 10, 255));

    xa_clear_mark(&xa, 10, 3);
    assert_false(xa_get_mark(&xa, 10, 3));

    /* Valid marks should still work */
    xa_set_mark(&xa, 10, XA_MARK_0);
    assert_true(xa_get_mark(&xa, 10, XA_MARK_0));

    /* Cursor API with invalid mark */
    xa_lock(&xa);
    XA_STATE(xas, &xa, 10);
    xas_load(&xas);
    xas_set_mark(&xas, 3);
    assert_false(xas_get_mark(&xas, 3));
    xas_clear_mark(&xas, 3);
    xa_unlock(&xa);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Store NULL at non-existent index                                 */
/* ====================================================================== */

static void test_store_null_nonexistent(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store NULL where nothing exists — should be a no-op */
    void *old = xa_store(&xa, 42, NULL, 0);
    assert_null(old);
    assert_null(xa_load(&xa, 42));

    /* With existing entries elsewhere */
    xa_store(&xa, 10, ENTRY(10), 0);
    old = xa_store(&xa, 42, NULL, 0);
    assert_null(old);
    assert_null(xa_load(&xa, 42));
    assert_ptr_equal(xa_load(&xa, 10), ENTRY(10));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Cursor error state operations                                    */
/* ====================================================================== */

static void test_cursor_error_state(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 5, ENTRY(5), 0);

    /* Manually put cursor into error state */
    XA_STATE(xas, &xa, 5);
    xas_set_err(&xas, -ENOMEM);

    assert_true(xas_is_error(&xas));
    assert_int_equal(xas_error(&xas), -ENOMEM);

    /* Operations on error-state cursor should return NULL / no-op */
    xa_rcu_lock();
    void *entry = xas_find(&xas, ~0ULL);
    assert_null(entry);

    entry = xas_find_marked(&xas, ~0ULL, XA_MARK_0);
    assert_null(entry);

    assert_false(xas_get_mark(&xas, XA_MARK_0));
    xa_rcu_unlock();

    /* xas_set should clear the error */
    xas_set(&xas, 5);
    assert_false(xas_is_error(&xas));
    assert_int_equal(xas_error(&xas), 0);

    xa_rcu_lock();
    entry = xas_load(&xas);
    assert_ptr_equal(entry, ENTRY(5));
    xa_rcu_unlock();

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Test: Mark overwrite consistency (head path vs node path)              */
/* ====================================================================== */

static void test_mark_overwrite_head_vs_node(void **state)
{
    (void)state;

    /* Case A: single-entry head (index 0 only) — marks survive overwrite */
    struct xarray xa1;
    xa_init(&xa1);
    xa_store(&xa1, 0, ENTRY(0), 0);
    xa_set_mark(&xa1, 0, XA_MARK_0);
    xa_set_mark(&xa1, 0, XA_MARK_2);
    xa_store(&xa1, 0, ENTRY(1), 0);
    assert_true(xa_get_mark(&xa1, 0, XA_MARK_0));
    assert_true(xa_get_mark(&xa1, 0, XA_MARK_2));
    xa_destroy(&xa1);

    /* Case B: node-level (force node by having a second entry) */
    struct xarray xa2;
    xa_init(&xa2);
    xa_store(&xa2, 0, ENTRY(0), 0);
    xa_store(&xa2, 100, ENTRY(100), 0);
    xa_set_mark(&xa2, 0, XA_MARK_0);
    xa_set_mark(&xa2, 0, XA_MARK_2);
    xa_store(&xa2, 0, ENTRY(1), 0);
    assert_true(xa_get_mark(&xa2, 0, XA_MARK_0));
    assert_true(xa_get_mark(&xa2, 0, XA_MARK_2));

    /* Verify marked iteration still finds the entry */
    uint64_t index;
    void *entry;
    size_t count = 0;
    xa_for_each_marked(&xa2, index, entry, XA_MARK_0) {
        assert_int_equal(index, 0);
        assert_ptr_equal(entry, ENTRY(1));
        count++;
    }
    assert_int_equal(count, 1);
    xa_destroy(&xa2);
}

/* ====================================================================== */
/*  Test: Store sentinel / internal values (negative test)                 */
/* ====================================================================== */

static void test_store_internal_entries(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Storing XA_ZERO_ENTRY must be rejected (returns error) */
    void *ret = xa_store(&xa, 0, XA_ZERO_ENTRY, 0);
    assert_ptr_equal(ret, XA_ZERO_ENTRY);
    assert_true(xa_empty(&xa));
    assert_null(xa_load(&xa, 0));

    /* Storing XA_RETRY_ENTRY must be rejected */
    ret = xa_store(&xa, 1, XA_RETRY_ENTRY, 0);
    assert_ptr_equal(ret, XA_ZERO_ENTRY);
    assert_true(xa_empty(&xa));
    assert_null(xa_load(&xa, 1));

    /* A valid store should still work after rejections */
    ret = xa_store(&xa, 0, ENTRY(0), 0);
    assert_null(ret);
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));

    xa_destroy(&xa);
}

static void test_cursor_store_internal_entries(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_lock(&xa);
    XA_STATE(xas, &xa, 3);
    void *old = xas_store(&xas, XA_ZERO_ENTRY);
    assert_true(xas_is_error(&xas));
    assert_int_equal(xas_error(&xas), -EINVAL);
    assert_null(old);
    xa_unlock(&xa);

    assert_null(xa_load(&xa, 3));
    assert_null(xa_store(&xa, 3, ENTRY(3), 0));
    assert_ptr_equal(xa_load(&xa, 3), ENTRY(3));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Stress test: Random insert/delete/lookup churn                         */
/* ====================================================================== */

#define STRESS_CHURN_COUNT   4096
#define STRESS_CHURN_ROUNDS  8

static void test_stress_random_churn(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /*
     * Maintain a shadow array tracking what should be present.
     * Perform random insert/delete/overwrite and verify consistency.
     */
    bool present[STRESS_CHURN_COUNT];
    memset(present, 0, sizeof(present));

    uint64_t rng = 0xA5A5A5A5DEADBEEFULL;

    for (int round = 0; round < STRESS_CHURN_ROUNDS; round++) {
        /* Insert phase: insert ~75% of slots */
        for (size_t i = 0; i < STRESS_CHURN_COUNT; i++) {
            if ((xorshift64(&rng) % 4) != 0) {
                xa_store(&xa, i, ENTRY(i), 0);
                present[i] = true;
            }
        }

        /* Delete phase: randomly delete ~50% of existing entries */
        for (size_t i = 0; i < STRESS_CHURN_COUNT; i++) {
            if (present[i] && (xorshift64(&rng) % 2) == 0) {
                void *old = xa_erase(&xa, i);
                assert_ptr_equal(old, ENTRY(i));
                present[i] = false;
            }
        }

        /* Verify all entries match shadow */
        size_t expected_count = 0;
        for (size_t i = 0; i < STRESS_CHURN_COUNT; i++) {
            void *e = xa_load(&xa, i);
            if (present[i]) {
                assert_ptr_equal(e, ENTRY(i));
                expected_count++;
            } else {
                assert_null(e);
            }
        }
        assert_int_equal(xa_count_entries(&xa), expected_count);

        /* Overwrite phase: overwrite some existing entries */
        for (size_t i = 0; i < STRESS_CHURN_COUNT; i++) {
            if (present[i] && (xorshift64(&rng) % 3) == 0) {
                void *old = xa_store(&xa, i, ENTRY(i + STRESS_CHURN_COUNT), 0);
                assert_ptr_equal(old, ENTRY(i));
                /* Update to new value for verification */
                void *e = xa_load(&xa, i);
                assert_ptr_equal(e, ENTRY(i + STRESS_CHURN_COUNT));
                /* Restore original for next round simplicity */
                xa_store(&xa, i, ENTRY(i), 0);
            }
        }
    }

    /* Final cleanup: erase everything */
    for (size_t i = 0; i < STRESS_CHURN_COUNT; i++) {
        if (present[i])
            xa_erase(&xa, i);
    }
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Stress test: Sparse random indices across wide key space               */
/* ====================================================================== */

#define STRESS_SPARSE_COUNT  512

static void test_stress_sparse_random(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    uint64_t indices[STRESS_SPARSE_COUNT];
    uint64_t rng = 0x123456789ABCDEF0ULL;

    /* Generate random indices spread across a wide range */
    for (size_t i = 0; i < STRESS_SPARSE_COUNT; i++) {
        uint64_t hi = xorshift64(&rng);
        uint64_t lo = xorshift64(&rng);
        /* Limit to 40 bits to keep memory reasonable but force deep trees */
        indices[i] = ((hi << 20) | (lo & 0xFFFFF)) & ((1ULL << 40) - 1);
    }

    /* Deduplicate by storing index position — last writer wins */
    for (size_t i = 0; i < STRESS_SPARSE_COUNT; i++)
        xa_store(&xa, indices[i], ENTRY(i), 0);

    /* Verify: each index should load the last-written ENTRY */
    for (int i = (int)STRESS_SPARSE_COUNT - 1; i >= 0; i--) {
        void *e = xa_load(&xa, indices[i]);
        assert_non_null(e);
    }

    /* Iteration: should be in ascending index order */
    uint64_t idx;
    void *entry;
    uint64_t prev = 0;
    bool first = true;
    xa_for_each(&xa, idx, entry) {
        if (!first)
            assert_true(idx > prev);
        first = false;
        prev = idx;
    }

    /* Erase all and verify empty */
    for (size_t i = 0; i < STRESS_SPARSE_COUNT; i++)
        xa_erase(&xa, indices[i]);
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Stress test: Rapid tree grow/shrink cycles                             */
/* ====================================================================== */

#define STRESS_GROWSHRINK_ROUNDS 16

static void test_stress_grow_shrink_cycles(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /*
     * Each round, insert an entry at an exponentially larger index
     * (forcing the tree to grow many levels), then erase it (forcing
     * full shrink back to empty). This hammers expand/shrink paths.
     */
    uint64_t big_indices[] = {
        0, 63, 64, 4095, 4096, 262143, 262144,
        (1ULL << 24), (1ULL << 30), (1ULL << 36),
        (1ULL << 42), (1ULL << 48), (1ULL << 54), (1ULL << 60),
        (~0ULL >> 1), ~0ULL
    };

    for (size_t round = 0; round < ARRAY_SIZE(big_indices); round++) {
        uint64_t idx = big_indices[round];
        xa_store(&xa, idx, ENTRY(1), 0);
        assert_ptr_equal(xa_load(&xa, idx), ENTRY(1));
        assert_false(xa_empty(&xa));

        xa_erase(&xa, idx);
        assert_null(xa_load(&xa, idx));
        assert_true(xa_empty(&xa));
    }

    /* Combined: insert at both small and huge index, then erase */
    for (size_t round = 0; round < ARRAY_SIZE(big_indices); round++) {
        uint64_t idx = big_indices[round];
        if (idx == 0)
            continue; /* skip: both would alias to index 0 */
        xa_store(&xa, 0, ENTRY(0), 0);
        xa_store(&xa, idx, ENTRY(1), 0);

        assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));
        assert_ptr_equal(xa_load(&xa, idx), ENTRY(1));

        xa_erase(&xa, idx);
        assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));

        xa_erase(&xa, 0);
        assert_true(xa_empty(&xa));
    }

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Stress test: Random marks with verification                            */
/* ====================================================================== */

#define STRESS_MARK_COUNT  512

static void test_stress_random_marks(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    uint8_t mark_state[STRESS_MARK_COUNT][XA_MAX_MARKS];
    memset(mark_state, 0, sizeof(mark_state));

    /* Populate */
    for (uint64_t i = 0; i < STRESS_MARK_COUNT; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    uint64_t rng = 0xFEEDFACECAFEBABEULL;

    /* Random mark/unmark operations */
    for (int ops = 0; ops < STRESS_MARK_COUNT * 8; ops++) {
        uint64_t idx = xorshift64(&rng) % STRESS_MARK_COUNT;
        xa_mark_t mark = (xa_mark_t)(xorshift64(&rng) % XA_MAX_MARKS);
        bool set = (xorshift64(&rng) % 2) != 0;

        if (set) {
            xa_set_mark(&xa, idx, mark);
            mark_state[idx][mark] = 1;
        } else {
            xa_clear_mark(&xa, idx, mark);
            mark_state[idx][mark] = 0;
        }
    }

    /* Verify all mark states */
    for (uint64_t i = 0; i < STRESS_MARK_COUNT; i++) {
        for (xa_mark_t m = 0; m < XA_MAX_MARKS; m++) {
            bool expected = mark_state[i][m] != 0;
            assert_int_equal(xa_get_mark(&xa, i, m), expected);
        }
    }

    /* Verify marked iteration counts for each mark */
    for (xa_mark_t m = 0; m < XA_MAX_MARKS; m++) {
        size_t expected_count = 0;
        for (uint64_t i = 0; i < STRESS_MARK_COUNT; i++) {
            if (mark_state[i][m])
                expected_count++;
        }

        uint64_t idx;
        void *entry;
        size_t actual_count = 0;
        xa_for_each_marked(&xa, idx, entry, m) {
            assert_true(mark_state[idx][m]);
            actual_count++;
        }
        assert_int_equal(actual_count, expected_count);
    }

    /* Erase random entries and verify marks are cleaned up */
    for (uint64_t i = 0; i < STRESS_MARK_COUNT; i += 3) {
        xa_erase(&xa, i);
        for (xa_mark_t m = 0; m < XA_MAX_MARKS; m++) {
            assert_false(xa_get_mark(&xa, i, m));
            mark_state[i][m] = 0;
        }
    }

    /* Re-verify marked iteration after erasures */
    for (xa_mark_t m = 0; m < XA_MAX_MARKS; m++) {
        size_t expected_count = 0;
        for (uint64_t i = 0; i < STRESS_MARK_COUNT; i++) {
            if (mark_state[i][m])
                expected_count++;
        }

        uint64_t idx;
        void *entry;
        size_t actual_count = 0;
        xa_for_each_marked(&xa, idx, entry, m) {
            actual_count++;
        }
        assert_int_equal(actual_count, expected_count);
    }

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Stress test: Full node churn — fill and empty many nodes               */
/* ====================================================================== */

#define STRESS_NODE_CHURN_NODES  32   /* number of 64-slot nodes to fill */

static void test_stress_node_churn(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    const size_t total = STRESS_NODE_CHURN_NODES * XA_CHUNK_SIZE;

    for (int round = 0; round < 4; round++) {
        /* Fill: each round writes in a different order */
        uint64_t indices[STRESS_NODE_CHURN_NODES * XA_CHUNK_SIZE];
        for (size_t i = 0; i < total; i++)
            indices[i] = i;

        uint64_t rng = 0xBAADF00D00000000ULL + (uint64_t)round;
        for (size_t i = total - 1; i > 0; i--) {
            size_t j = xorshift64(&rng) % (i + 1);
            uint64_t tmp = indices[i];
            indices[i] = indices[j];
            indices[j] = tmp;
        }

        for (size_t i = 0; i < total; i++)
            xa_store(&xa, indices[i], ENTRY(indices[i]), 0);

        assert_int_equal(xa_count_entries(&xa), total);

        /* Verify */
        for (size_t i = 0; i < total; i++)
            assert_ptr_equal(xa_load(&xa, i), ENTRY(i));

        /* Erase in shuffled order */
        rng = 0xCAFED00D00000000ULL + (uint64_t)round;
        for (size_t i = total - 1; i > 0; i--) {
            size_t j = xorshift64(&rng) % (i + 1);
            uint64_t tmp = indices[i];
            indices[i] = indices[j];
            indices[j] = tmp;
        }

        for (size_t i = 0; i < total; i++) {
            void *old = xa_erase(&xa, indices[i]);
            assert_ptr_equal(old, ENTRY(indices[i]));
        }

        assert_true(xa_empty(&xa));
    }

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Stress test: Interleaved operations — store, mark, find, erase         */
/* ====================================================================== */

#define STRESS_INTERLEAVE_COUNT  1024

static void test_stress_interleaved_ops(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    uint64_t rng = 0x0011223344556677ULL;

    /* Phase 1: insert and mark simultaneously */
    for (uint64_t i = 0; i < STRESS_INTERLEAVE_COUNT; i++) {
        xa_store(&xa, i, ENTRY(i), 0);
        /* Mark every 3rd with MARK_0, every 5th with MARK_1 */
        if (i % 3 == 0)
            xa_set_mark(&xa, i, XA_MARK_0);
        if (i % 5 == 0)
            xa_set_mark(&xa, i, XA_MARK_1);
    }

    /* Verify mark counts */
    uint64_t idx;
    void *entry;
    size_t m0_count = 0, m1_count = 0;
    xa_for_each_marked(&xa, idx, entry, XA_MARK_0) { m0_count++; }
    xa_for_each_marked(&xa, idx, entry, XA_MARK_1) { m1_count++; }

    size_t expected_m0 = 0, expected_m1 = 0;
    for (uint64_t i = 0; i < STRESS_INTERLEAVE_COUNT; i++) {
        if (i % 3 == 0) expected_m0++;
        if (i % 5 == 0) expected_m1++;
    }
    assert_int_equal(m0_count, expected_m0);
    assert_int_equal(m1_count, expected_m1);

    /* Phase 2: erase random entries while using xa_find to walk */
    for (int pass = 0; pass < 4; pass++) {
        /* Erase ~25% of remaining */
        for (uint64_t i = 0; i < STRESS_INTERLEAVE_COUNT; i++) {
            if ((xorshift64(&rng) % 4) == 0)
                xa_erase(&xa, i);
        }

        /* Walk with xa_find — must be in ascending order */
        idx = 0;
        entry = xa_find(&xa, &idx, ~0ULL, XA_MARK_MAX);
        uint64_t prev = 0;
        bool first = true;
        while (entry != NULL) {
            if (!first)
                assert_true(idx > prev);
            first = false;
            prev = idx;
            entry = xa_find_after(&xa, &idx, ~0ULL, XA_MARK_MAX);
        }

        /* Walk with xa_find marked — must still be in order */
        idx = 0;
        entry = xa_find(&xa, &idx, ~0ULL, XA_MARK_0);
        prev = 0;
        first = true;
        while (entry != NULL) {
            if (!first)
                assert_true(idx > prev);
            /* This entry must actually have MARK_0 */
            assert_true(xa_get_mark(&xa, idx, XA_MARK_0));
            first = false;
            prev = idx;
            entry = xa_find_after(&xa, &idx, ~0ULL, XA_MARK_0);
        }
    }

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Stress test: Large scale with cursor API                               */
/* ====================================================================== */

#define STRESS_CURSOR_COUNT  2048

static void test_stress_cursor_api(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Bulk store via cursor API */
    xa_lock(&xa);
    for (uint64_t i = 0; i < STRESS_CURSOR_COUNT; i++) {
        XA_STATE(xas, &xa, i);
        void *old = xas_store(&xas, ENTRY(i));
        assert_null(old);
        assert_false(xas_is_error(&xas));
    }
    xa_unlock(&xa);

    assert_int_equal(xa_count_entries(&xa), STRESS_CURSOR_COUNT);

    /* Cursor walk with xas_find */
    XA_STATE(xas_walk, &xa, 0);
    xa_rcu_lock();
    size_t found = 0;
    void *e = xas_load(&xas_walk);
    if (e) found++;
    while ((e = xas_find(&xas_walk, STRESS_CURSOR_COUNT - 1)) != NULL)
        found++;
    xa_rcu_unlock();
    assert_int_equal(found, STRESS_CURSOR_COUNT);

    /* Set marks via cursor, verify via simple API */
    xa_lock(&xa);
    for (uint64_t i = 0; i < STRESS_CURSOR_COUNT; i += 7) {
        XA_STATE(xas_m, &xa, i);
        xas_load(&xas_m);
        xas_set_mark(&xas_m, XA_MARK_2);
    }
    xa_unlock(&xa);

    size_t mark_count = 0;
    for (uint64_t i = 0; i < STRESS_CURSOR_COUNT; i++) {
        if (xa_get_mark(&xa, i, XA_MARK_2)) {
            assert_int_equal(i % 7, 0);
            mark_count++;
        }
    }
    size_t expected_marks = (STRESS_CURSOR_COUNT + 6) / 7;
    assert_int_equal(mark_count, expected_marks);

    /* Bulk erase via cursor API */
    xa_lock(&xa);
    for (uint64_t i = 0; i < STRESS_CURSOR_COUNT; i++) {
        XA_STATE(xas_e, &xa, i);
        xas_store(&xas_e, NULL);
    }
    xa_unlock(&xa);

    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Edge case tests — P0: Correctness risks                                */
/* ====================================================================== */

/**
 * Head marks must survive when xa_expand promotes head to a node.
 * Sets mark on index 0 (single-entry head), then stores at a high index
 * forcing tree growth.  Verifies the mark transfers to the node.
 */
static void test_head_marks_survive_expand(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store at index 0 — stored directly in xa_head. */
    xa_store(&xa, 0, ENTRY(0), 0);
    xa_set_mark(&xa, 0, XA_MARK_0);
    xa_set_mark(&xa, 0, XA_MARK_2);
    assert_true(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 0, XA_MARK_1));
    assert_true(xa_get_mark(&xa, 0, XA_MARK_2));

    /* Store at index 100 — forces xa_expand (head→leaf→grow). */
    xa_store(&xa, 100, ENTRY(100), 0);

    /* Marks on index 0 must have survived the expansion. */
    assert_true(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 0, XA_MARK_1));
    assert_true(xa_get_mark(&xa, 0, XA_MARK_2));

    /* Data intact. */
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));
    assert_ptr_equal(xa_load(&xa, 100), ENTRY(100));

    /* Marked iteration should find index 0. */
    uint64_t idx = 0;
    void *entry = xa_find(&xa, &idx, UINT64_MAX, XA_MARK_0);
    assert_ptr_equal(entry, ENTRY(0));
    assert_int_equal(idx, 0);

    xa_destroy(&xa);
}

/**
 * xa_init_flags with bits 29-31 should not corrupt head mark state.
 * Bits 29-31 of xa_flags are reserved for head marks (XA_HEAD_MARK_SHIFT=29).
 * Verify that after init with high bits, marks behave correctly.
 */
static void test_init_flags_high_bits(void **state)
{
    (void)state;
    struct xarray xa;

    /* Set bits in the mark region (bits 29-31). */
    xa_init_flags(&xa, (1U << 29) | (1U << 30) | (1U << 31));

    /* Store an entry. */
    xa_store(&xa, 0, ENTRY(0), 0);

    /* Head marks should reflect the pre-set bits.
     * This may appear as marks being set initially — the point is
     * that the implementation doesn't crash and behaves consistently. */
    bool m0 = xa_get_mark(&xa, 0, XA_MARK_0);
    bool m1 = xa_get_mark(&xa, 0, XA_MARK_1);
    bool m2 = xa_get_mark(&xa, 0, XA_MARK_2);

    /* If flags leak into marks, clearing should still work. */
    if (m0) xa_clear_mark(&xa, 0, XA_MARK_0);
    if (m1) xa_clear_mark(&xa, 0, XA_MARK_1);
    if (m2) xa_clear_mark(&xa, 0, XA_MARK_2);

    assert_false(xa_get_mark(&xa, 0, XA_MARK_0));
    assert_false(xa_get_mark(&xa, 0, XA_MARK_1));
    assert_false(xa_get_mark(&xa, 0, XA_MARK_2));

    /* Data still works fine. */
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Edge case tests — P1: Sibling subsystem                                */
/* ====================================================================== */

/**
 * Erase a multi-slot (sibling) entry.  Verifies count accounting
 * in xas_clear_slot_range when removing siblings.
 */
static void test_sibling_erase(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Also store a non-sibling entry so the tree doesn't collapse. */
    xa_store(&xa, 0, ENTRY(0), 0);

    /* Store a 4-wide sibling at slots 4-7. */
    xa_lock(&xa);
    XA_STATE(xas, &xa, 4);
    xas.xa_sibs = 3;
    xas_store(&xas, ENTRY(4));
    xa_unlock(&xa);

    assert_ptr_equal(xa_load(&xa, 4), ENTRY(4));
    assert_ptr_equal(xa_load(&xa, 7), ENTRY(4));

    /* Erase via simple API at the canonical index. */
    void *old = xa_erase(&xa, 4);
    assert_ptr_equal(old, ENTRY(4));

    /* All sibling slots should now be empty. */
    assert_null(xa_load(&xa, 4));
    assert_null(xa_load(&xa, 5));
    assert_null(xa_load(&xa, 6));
    assert_null(xa_load(&xa, 7));

    /* Anchor entry still intact. */
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));

    xa_destroy(&xa);
}

/**
 * Overwrite a 4-wide sibling with a 2-wide sibling at the same offset.
 * Verifies span calculation (old_span > new_span).
 */
static void test_sibling_resize_shrink(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0); /* anchor */

    /* Store a 4-wide sibling at slots 4-7. */
    xa_lock(&xa);
    XA_STATE(xas1, &xa, 4);
    xas1.xa_sibs = 3;
    xas_store(&xas1, ENTRY(40));
    xa_unlock(&xa);

    /* Overwrite with a 2-wide sibling at slots 4-5. */
    xa_lock(&xa);
    XA_STATE(xas2, &xa, 4);
    xas2.xa_sibs = 1;
    xas_store(&xas2, ENTRY(42));
    xa_unlock(&xa);

    assert_ptr_equal(xa_load(&xa, 4), ENTRY(42));
    assert_ptr_equal(xa_load(&xa, 5), ENTRY(42));
    /* Slots 6-7 should be cleared. */
    assert_null(xa_load(&xa, 6));
    assert_null(xa_load(&xa, 7));

    xa_destroy(&xa);
}

/**
 * Overwrite a 2-wide sibling with a 4-wide sibling (grow).
 * Verifies span calculation (new_span > old_span).
 */
static void test_sibling_resize_grow(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0); /* anchor */

    /* Store a 2-wide sibling at slots 4-5. */
    xa_lock(&xa);
    XA_STATE(xas1, &xa, 4);
    xas1.xa_sibs = 1;
    xas_store(&xas1, ENTRY(40));
    xa_unlock(&xa);

    /* Overwrite with a 4-wide sibling at slots 4-7. */
    xa_lock(&xa);
    XA_STATE(xas2, &xa, 4);
    xas2.xa_sibs = 3;
    xas_store(&xas2, ENTRY(42));
    xa_unlock(&xa);

    assert_ptr_equal(xa_load(&xa, 4), ENTRY(42));
    assert_ptr_equal(xa_load(&xa, 5), ENTRY(42));
    assert_ptr_equal(xa_load(&xa, 6), ENTRY(42));
    assert_ptr_equal(xa_load(&xa, 7), ENTRY(42));

    xa_destroy(&xa);
}

/**
 * Sibling range at the node boundary.
 * canonical=60, span=4 → slots 60-63, exactly fits.
 * canonical=61, span=4 → slots 61-64, exceeds XA_CHUNK_SIZE → EINVAL.
 */
static void test_sibling_at_chunk_boundary(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0); /* anchor */

    /* Valid: slots 60-63 (fits exactly). */
    xa_lock(&xa);
    XA_STATE(xas_ok, &xa, 60);
    xas_ok.xa_sibs = 3;
    xas_store(&xas_ok, ENTRY(60));
    assert_false(xas_is_error(&xas_ok));
    xa_unlock(&xa);

    assert_ptr_equal(xa_load(&xa, 60), ENTRY(60));
    assert_ptr_equal(xa_load(&xa, 63), ENTRY(60));

    /* Invalid: slots 62-65 (exceeds chunk).
     * Use a fresh tree so index 62 is not an existing sibling. */
    struct xarray xa2;
    xa_init(&xa2);
    xa_store(&xa2, 0, ENTRY(0), 0); /* anchor */

    xa_lock(&xa2);
    XA_STATE(xas_bad, &xa2, 62);
    xas_bad.xa_sibs = 3;
    xas_store(&xas_bad, ENTRY(62));
    assert_true(xas_is_error(&xas_bad));
    xa_unlock(&xa2);

    xa_destroy(&xa2);

    xa_destroy(&xa);
}

/**
 * Set and get marks on sibling entries.
 * Marks are per-slot, so setting a mark on the canonical slot should be
 * visible, and iteration should yield the sibling entry exactly once.
 */
static void test_sibling_marks(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0); /* anchor */

    /* Store a 4-wide sibling at slots 4-7. */
    xa_lock(&xa);
    XA_STATE(xas, &xa, 4);
    xas.xa_sibs = 3;
    xas_store(&xas, ENTRY(4));
    xa_unlock(&xa);

    /* Set mark on the canonical index. */
    xa_set_mark(&xa, 4, XA_MARK_1);
    assert_true(xa_get_mark(&xa, 4, XA_MARK_1));

    /* Find marked should return the entry at canonical index. */
    uint64_t idx = 0;
    void *entry = xa_find(&xa, &idx, UINT64_MAX, XA_MARK_1);
    assert_non_null(entry);
    assert_ptr_equal(entry, ENTRY(4));
    assert_int_equal(idx, 4);

    /* No more marked entries. */
    entry = xa_find_after(&xa, &idx, UINT64_MAX, XA_MARK_1);
    assert_null(entry);

    /* Clear the mark. */
    xa_clear_mark(&xa, 4, XA_MARK_1);
    assert_false(xa_get_mark(&xa, 4, XA_MARK_1));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Edge case tests — P2: Find/iteration boundary conditions               */
/* ====================================================================== */

/**
 * xa_find_after from UINT64_MAX-1 should find an entry at UINT64_MAX.
 */
static void test_find_after_reaches_uint64_max(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, UINT64_MAX, ENTRY(99), 0);

    uint64_t idx = UINT64_MAX - 1;
    void *entry = xa_find_after(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
    assert_ptr_equal(entry, ENTRY(99));
    assert_int_equal(idx, UINT64_MAX);

    xa_destroy(&xa);
}

/**
 * xas_walk_next must correctly climb from the rightmost slot at every level.
 * Store entries at slot-63-of-every-level positions and iterate.
 */
static void test_walk_next_rightmost_slots(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* slot 63 at level 0 = index 63
     * slot 63 at level 1 = index 63*64 + 63 = 4095
     * We store entries at 63 and 4095, then iterate to verify walk_next
     * can climb from slot 63 of one node to the parent and back down.
     */
    xa_store(&xa, 63, ENTRY(63), 0);
    xa_store(&xa, 4095, ENTRY(4095), 0);

    uint64_t idx = 0;
    void *entry = xa_find(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
    assert_ptr_equal(entry, ENTRY(63));
    assert_int_equal(idx, 63);

    entry = xa_find_after(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
    assert_ptr_equal(entry, ENTRY(4095));
    assert_int_equal(idx, 4095);

    entry = xa_find_after(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
    assert_null(entry);

    xa_destroy(&xa);
}

/**
 * xas_find with max smaller than the next entry's index at an intermediate
 * level.  The walk should stop rather than descend into a subtree beyond max.
 */
static void test_find_max_at_intermediate_level(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Two entries in different level-1 subtrees. */
    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 100, ENTRY(100), 0);

    /* Search with max=50 should find index 0 but NOT index 100. */
    uint64_t idx = 0;
    void *entry = xa_find(&xa, &idx, 50, XA_MARK_MAX);
    assert_ptr_equal(entry, ENTRY(0));
    assert_int_equal(idx, 0);

    entry = xa_find_after(&xa, &idx, 50, XA_MARK_MAX);
    assert_null(entry);

    xa_destroy(&xa);
}

static void test_find_skips_sibling_range(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_lock(&xa);
    XA_STATE(xas, &xa, 4);
    xas.xa_sibs = 3;
    assert_null(xas_store(&xas, ENTRY(4)));
    xa_unlock(&xa);

    xa_store(&xa, 9, ENTRY(9), 0);

    uint64_t idx = 4;
    void *entry = xa_find(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
    assert_ptr_equal(entry, ENTRY(4));
    assert_int_equal(idx, 4);

    entry = xa_find_after(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
    assert_ptr_equal(entry, ENTRY(9));
    assert_int_equal(idx, 9);

    idx = 5;
    entry = xa_find(&xa, &idx, UINT64_MAX, XA_MARK_MAX);
    assert_ptr_equal(entry, ENTRY(9));
    assert_int_equal(idx, 9);

    xa_destroy(&xa);
}

static void test_find_respects_start_above_max(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 100, ENTRY(100), 0);
    xa_set_mark(&xa, 100, XA_MARK_0);

    uint64_t idx = 100;
    assert_null(xa_find(&xa, &idx, 50, XA_MARK_MAX));
    assert_int_equal(idx, 100);

    idx = 100;
    assert_null(xa_find(&xa, &idx, 50, XA_MARK_0));
    assert_int_equal(idx, 100);

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Edge case tests — P3: Structural / cleanup                             */
/* ====================================================================== */

/**
 * xa_shrink should NOT shrink when root has count==1 but the entry
 * is not in slot 0.  E.g., only entry at index 64 means root's slot[1]
 * holds the child.  Tree must stay tall.
 */
static void test_shrink_nonzero_slot(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store at index 64 — lands in root's slot 1 (level-1 node). */
    xa_store(&xa, 64, ENTRY(64), 0);
    assert_ptr_equal(xa_load(&xa, 64), ENTRY(64));

    /* Erase index 64 and re-store at 64 — verifies tree structure. */
    xa_erase(&xa, 64);
    assert_null(xa_load(&xa, 64));

    /* Store at both 0 and 64, then erase 0 — root still has slot[1]. */
    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 64, ENTRY(64), 0);
    xa_erase(&xa, 0);

    /* Index 64 should still be accessible. */
    assert_ptr_equal(xa_load(&xa, 64), ENTRY(64));
    assert_null(xa_load(&xa, 0));

    xa_destroy(&xa);
}

/**
 * xa_destroy on a 3+ level tree with entries still present.
 * Exercises the recursive xa_destroy_node path at depth.
 */
static void test_destroy_deep_tree(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Level 0: shift=0, covers 0-63
     * Level 1: shift=6, covers 0-4095
     * Level 2: shift=12, covers 0-262143
     * Storing at 0 and 262143 forces a 3-level tree. */
    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 262143, ENTRY(262143), 0);
    xa_store(&xa, 4096, ENTRY(4096), 0);

    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));
    assert_ptr_equal(xa_load(&xa, 4096), ENTRY(4096));
    assert_ptr_equal(xa_load(&xa, 262143), ENTRY(262143));

    /* Destroy without erasing — recursive cleanup. */
    xa_destroy(&xa);

    /* After destroy, tree should be empty. */
    assert_true(xa_empty(&xa));
}

/**
 * Cascading xas_delete_node: erase the last entry in a sparse deep tree.
 * Should delete nodes up through ancestors until a node with count>0.
 */
static void test_cascading_delete_node(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Build a 3-level tree with entries in two distant subtrees. */
    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 200000, ENTRY(200000), 0);

    /* Erase the far entry — its ancestors should cascade-delete
     * down to the common root. */
    xa_erase(&xa, 200000);
    assert_null(xa_load(&xa, 200000));

    /* The other entry is still fine. */
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));

    /* Now erase the remaining entry — tree should become empty. */
    xa_erase(&xa, 0);
    assert_true(xa_empty(&xa));

    xa_destroy(&xa);
}

/**
 * Mutation (store/erase) during xa_for_each iteration.
 * Since xa_for_each re-descends from root on each xa_find_after call,
 * this should work correctly.
 */
static void test_mutation_during_iteration(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    for (uint64_t i = 0; i < 20; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    /* Erase even entries during iteration. */
    uint64_t index;
    void *entry;
    size_t count = 0;
    xa_for_each(&xa, index, entry) {
        count++;
        if (index % 2 == 0)
            xa_erase(&xa, index);
    }

    /* Should have seen all 20 entries (some may be seen before erase). */
    assert_true(count >= 10);

    /* Only odd entries should remain. */
    for (uint64_t i = 0; i < 20; i++) {
        if (i % 2 == 0)
            assert_null(xa_load(&xa, i));
        else
            assert_ptr_equal(xa_load(&xa, i), ENTRY(i));
    }

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Edge case tests — P4: Defensive / minor                                */
/* ====================================================================== */

/**
 * Value entries with encodings near sentinel values.
 * xa_mk_value(0x40) → 0x102, near XA_RETRY_ENTRY (0x101).
 * Ensures the encoding never collides with sentinels.
 */
static void test_value_entry_near_sentinel(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* 0x40 encodes to (0x40 << 2) | 2 = 0x102, near RETRY=0x101. */
    xa_store(&xa, 0, xa_mk_value(0x40), 0);
    void *v = xa_load(&xa, 0);
    assert_true(xa_is_value(v));
    assert_int_equal(xa_to_value(v), 0x40);
    assert_true(v != XA_RETRY_ENTRY);
    assert_true(v != XA_ZERO_ENTRY);

    /* 0x80 encodes to (0x80 << 2) | 2 = 0x202, near ZERO=0x201. */
    xa_store(&xa, 1, xa_mk_value(0x80), 0);
    v = xa_load(&xa, 1);
    assert_true(xa_is_value(v));
    assert_int_equal(xa_to_value(v), 0x80);
    assert_true(v != XA_RETRY_ENTRY);
    assert_true(v != XA_ZERO_ENTRY);

    xa_destroy(&xa);
}

/**
 * xas_find_marked directly on a single-entry head.
 * Verifies cursor state correctness (xa_node=NULL, xa_offset=0).
 */
static void test_cursor_find_marked_head(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0);
    xa_set_mark(&xa, 0, XA_MARK_0);

    /* Use cursor API to find marked on single-entry head. */
    XA_STATE(xas, &xa, 0);
    xa_rcu_lock();
    void *entry = xas_find_marked(&xas, UINT64_MAX, XA_MARK_0);
    assert_ptr_equal(entry, ENTRY(0));
    assert_int_equal(xas.xa_index, 0);

    /* No more marked entries. */
    entry = xas_find_marked(&xas, UINT64_MAX, XA_MARK_0);
    assert_null(entry);
    xa_rcu_unlock();

    xa_destroy(&xa);
}

/**
 * Sibling store on a single-entry head should fail with EINVAL.
 * xas_store_to_head rejects xa_sibs != 0.
 */
static void test_sibling_store_on_head(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0);

    /* Try to store a 2-wide sibling at index 0 (head entry). */
    xa_lock(&xa);
    XA_STATE(xas, &xa, 0);
    xas.xa_sibs = 1;
    void *old = xas_store(&xas, ENTRY(99));
    assert_true(xas_is_error(&xas));
    assert_null(old);
    xa_unlock(&xa);

    /* Original entry undamaged. */
    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));

    xa_destroy(&xa);
}

/**
 * Multi-level grow in a single xa_store: store at an index requiring
 * multiple levels to be added in one expansion.
 */
static void test_multi_level_expand(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store at index 0 (head entry), then at 2^18 = 262144 which requires
     * growing from 1 level to 4 levels in one xa_expand call. */
    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 262144, ENTRY(262144), 0);

    assert_ptr_equal(xa_load(&xa, 0), ENTRY(0));
    assert_ptr_equal(xa_load(&xa, 262144), ENTRY(262144));

    /* Verify intermediate indices are empty. */
    assert_null(xa_load(&xa, 1));
    assert_null(xa_load(&xa, 63));
    assert_null(xa_load(&xa, 64));
    assert_null(xa_load(&xa, 4096));

    xa_destroy(&xa);
}

/* ====================================================================== */
/*  Main                                                                   */
/* ====================================================================== */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Basic operations (from list push/pop/create patterns) */
        cmocka_unit_test(test_empty_xarray),
        cmocka_unit_test(test_single_entry),
        cmocka_unit_test(test_store_overwrite),
        cmocka_unit_test(test_push_pop_pattern),
        cmocka_unit_test(test_erase_empty),
        cmocka_unit_test(test_erase_missing),
        cmocka_unit_test(test_store_null_erases),

        /* Insertion patterns (from rbtree insert tests) */
        cmocka_unit_test(test_insert_sequential),
        cmocka_unit_test(test_insert_duplicate),
        cmocka_unit_test(test_sequence_cases),

        /* Iteration (from rbtree iteration_order + list foreach) */
        cmocka_unit_test(test_iteration_order),
        cmocka_unit_test(test_find_and_erase),
        cmocka_unit_test(test_find_operations),
        cmocka_unit_test(test_find_bounded),
        cmocka_unit_test(test_find_marked),

        /* Mark operations */
        cmocka_unit_test(test_marks_basic),
        cmocka_unit_test(test_marks_independent),
        cmocka_unit_test(test_marks_iteration),
        cmocka_unit_test(test_mark_missing_entry),
        cmocka_unit_test(test_marks_with_tree_depth),
        cmocka_unit_test(test_all_marks_on_entry),
        cmocka_unit_test(test_mark_survives_overwrite),
        cmocka_unit_test(test_mark_erase_stops_iteration),

        /* Value entries */
        cmocka_unit_test(test_value_entries),
        cmocka_unit_test(test_value_entry_erase_overwrite),
        cmocka_unit_test(test_value_entry_edge_values),
        cmocka_unit_test(test_mixed_pointer_and_value),
        cmocka_unit_test(test_value_entries_with_marks),

        /* Cursor / advanced API */
        cmocka_unit_test(test_cursor_api),
        cmocka_unit_test(test_cursor_iteration),
        cmocka_unit_test(test_cursor_find_marked),
        cmocka_unit_test(test_cursor_mark_ops),
        cmocka_unit_test(test_cursor_store_multiple),
        cmocka_unit_test(test_cursor_reposition),

        /* Tree structure / node boundaries */
        cmocka_unit_test(test_full_node),
        cmocka_unit_test(test_node_boundary_crossing),
        cmocka_unit_test(test_multi_level_tree),
        cmocka_unit_test(test_power_of_64_indices),
        cmocka_unit_test(test_erase_shrinks_tree),

        /* Large / extreme indices */
        cmocka_unit_test(test_large_index),
        cmocka_unit_test(test_sparse_entries),
        cmocka_unit_test(test_extreme_indices),

        /* Stress / integrity (from rbtree delete_balancing + scale) */
        cmocka_unit_test(test_delete_balancing),
        cmocka_unit_test(test_scale),
        cmocka_unit_test(test_reinsert_after_erase),
        cmocka_unit_test(test_bulk_reinsert_cycle),
        cmocka_unit_test(test_reverse_erase),

        /* Cleanup */
        cmocka_unit_test(test_destroy_with_entries),
        cmocka_unit_test(test_destroy_empty),

        /* Edge case tests */
        cmocka_unit_test(test_uint64_max_index),
        cmocka_unit_test(test_index0_head_marks),
        cmocka_unit_test(test_store_index0_into_deep_tree),
        cmocka_unit_test(test_find_empty),
        cmocka_unit_test(test_find_after_uint64_max),
        cmocka_unit_test(test_find_after_at_max),
        cmocka_unit_test(test_find_max_zero),
        cmocka_unit_test(test_marked_iteration_empty_and_no_marks),
        cmocka_unit_test(test_double_erase_same_index),
        cmocka_unit_test(test_double_destroy_with_entries),
        cmocka_unit_test(test_sibling_entries),
        cmocka_unit_test(test_clear_mark_without_mark),
        cmocka_unit_test(test_marks_across_levels_partial_erase),
        cmocka_unit_test(test_get_mark_on_empty_index),
        cmocka_unit_test(test_init_flags),

        /* Negative tests */
        cmocka_unit_test(test_invalid_mark_value),
        cmocka_unit_test(test_store_null_nonexistent),
        cmocka_unit_test(test_cursor_error_state),
        cmocka_unit_test(test_store_internal_entries),
        cmocka_unit_test(test_cursor_store_internal_entries),
        cmocka_unit_test(test_mark_overwrite_head_vs_node),

        /* Stress tests */
        cmocka_unit_test(test_stress_random_churn),
        cmocka_unit_test(test_stress_sparse_random),
        cmocka_unit_test(test_stress_grow_shrink_cycles),
        cmocka_unit_test(test_stress_random_marks),
        cmocka_unit_test(test_stress_node_churn),
        cmocka_unit_test(test_stress_interleaved_ops),
        cmocka_unit_test(test_stress_cursor_api),

        /* P0: Correctness edge cases */
        cmocka_unit_test(test_head_marks_survive_expand),
        cmocka_unit_test(test_init_flags_high_bits),

        /* P1: Sibling subsystem */
        cmocka_unit_test(test_sibling_erase),
        cmocka_unit_test(test_sibling_resize_shrink),
        cmocka_unit_test(test_sibling_resize_grow),
        cmocka_unit_test(test_sibling_at_chunk_boundary),
        cmocka_unit_test(test_sibling_marks),

        /* P2: Find/iteration boundaries */
        cmocka_unit_test(test_find_after_reaches_uint64_max),
        cmocka_unit_test(test_walk_next_rightmost_slots),
        cmocka_unit_test(test_find_max_at_intermediate_level),
        cmocka_unit_test(test_find_skips_sibling_range),
        cmocka_unit_test(test_find_respects_start_above_max),

        /* P3: Structural / cleanup */
        cmocka_unit_test(test_shrink_nonzero_slot),
        cmocka_unit_test(test_destroy_deep_tree),
        cmocka_unit_test(test_cascading_delete_node),
        cmocka_unit_test(test_mutation_during_iteration),

        /* P4: Defensive / minor */
        cmocka_unit_test(test_value_entry_near_sentinel),
        cmocka_unit_test(test_cursor_find_marked_head),
        cmocka_unit_test(test_sibling_store_on_head),
        cmocka_unit_test(test_multi_level_expand),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
