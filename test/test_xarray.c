/**
 * @file test_xarray.c
 * @brief Comprehensive XArray unit tests using cmocka.
 *
 * Test patterns ported from the xv6 rbtree and list test suites,
 * adapted for the xarray radix-tree data structure.
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

static void test_mark_cleared_on_overwrite(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 5, ENTRY(5), 0);
    xa_set_mark(&xa, 5, XA_MARK_0);
    assert_true(xa_get_mark(&xa, 5, XA_MARK_0));

    /* Overwrite entry — xas_store clears marks before re-storing */
    xa_store(&xa, 5, ENTRY(50), 0);
    assert_ptr_equal(xa_load(&xa, 5), ENTRY(50));
    assert_false(xa_get_mark(&xa, 5, XA_MARK_0));

    /* Set mark again, erase — mark should be gone */
    xa_set_mark(&xa, 5, XA_MARK_0);
    assert_true(xa_get_mark(&xa, 5, XA_MARK_0));
    xa_erase(&xa, 5);
    assert_false(xa_get_mark(&xa, 5, XA_MARK_0));

    /* Re-insert — should not have mark */
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
        cmocka_unit_test(test_mark_cleared_on_overwrite),
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
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
