/**
 * @file test_xarray_rcu.c
 * @brief RCU-enabled test suite — runs ALL test cases under RCU mode.
 *
 * Implements a primitive RCU mechanism where the grace period spans each
 * individual test.  Deferred-free objects accumulate in a global linked
 * list (mutex-protected) and are drained in a per-test teardown.
 *
 * All 94 test functions from test_xarray.c are pulled in via #include
 * and re-registered with the RCU teardown.  Two additional RCU-specific
 * tests exercise deferred-free accounting and lock-free reader concurrency.
 *
 * Build with: -DXA_CONFIG_RCU -DXA_CUSTOM_RCU
 */

/* ====================================================================== */
/*  Primitive RCU implementation                                           */
/*                                                                         */
/*  Must be defined BEFORE xarray.h is included (via the #include of       */
/*  test_xarray.c below), because xarray_config.h with XA_CUSTOM_RCU      */
/*  declares these as extern.                                              */
/* ====================================================================== */

#include <stdlib.h>
#include <pthread.h>

struct rcu_entry {
    struct rcu_entry   *next;
    void              (*cb)(void *);
    void               *data;
};

static pthread_mutex_t rcu_global_lock = PTHREAD_MUTEX_INITIALIZER;
static struct rcu_entry *rcu_global_list;

void xa_rcu_read_lock(void)   { /* no-op: entire test is the grace period */ }
void xa_rcu_read_unlock(void) { /* no-op */ }

/*
 * Append to a global mutex-protected list so that deferred frees from
 * worker threads (which may exit before drain) are never lost.
 */
void xa_call_rcu(void (*cb)(void *), void *data)
{
    struct rcu_entry *e = malloc(sizeof(*e));
    if (!e)
        abort();
    e->cb   = cb;
    e->data = data;

    pthread_mutex_lock(&rcu_global_lock);
    e->next = rcu_global_list;
    rcu_global_list = e;
    pthread_mutex_unlock(&rcu_global_lock);
}

/**
 * xa_rcu_drain - End the grace period: execute every deferred callback.
 * Returns the number of objects freed.
 */
static size_t xa_rcu_drain(void)
{
    pthread_mutex_lock(&rcu_global_lock);
    struct rcu_entry *e = rcu_global_list;
    rcu_global_list = NULL;
    pthread_mutex_unlock(&rcu_global_lock);

    size_t count = 0;
    while (e) {
        struct rcu_entry *next = e->next;
        e->cb(e->data);
        free(e);
        e = next;
        count++;
    }
    return count;
}

/* ====================================================================== */
/*  Pull in all 94 test functions from the non-RCU test file               */
/* ====================================================================== */

/* Rename the original main() so we can supply our own. */
#define main test_main_non_rcu_unused_
#include "test_xarray.c"
#undef main

/* ====================================================================== */
/*  RCU-specific tests                                                     */
/* ====================================================================== */

/**
 * Verify that operations produce deferred frees (not immediate).
 * Insert enough keys to trigger node creation + deletion, then check
 * that xa_rcu_drain() finds deferred objects.
 */
static void test_rcu_deferred_accounting(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Store entries to build a multi-level tree. */
    for (uint64_t i = 0; i < 200; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    /* Erase all to trigger node deletion => deferred frees. */
    for (uint64_t i = 0; i < 200; i++)
        xa_erase(&xa, i);

    /* Drain should find deferred dead nodes. */
    size_t freed = xa_rcu_drain();
    assert_true(freed > 0);

    /* Tree is now empty and still usable. */
    assert_true(xa_empty(&xa));
    xa_store(&xa, 42, ENTRY(42), 0);
    assert_ptr_equal(xa_load(&xa, 42), ENTRY(42));

    xa_destroy(&xa);
    xa_rcu_drain();
}

static void test_rcu_destroy_defers_reclamation(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    xa_store(&xa, 0, ENTRY(0), 0);
    xa_store(&xa, 4096, ENTRY(4096), 0);
    xa_store(&xa, 262144, ENTRY(262144), 0);

    xa_destroy(&xa);
    assert_true(xa_empty(&xa));
    assert_true(xa_rcu_drain() > 0);
}

/**
 * Core RCU scenario: lock-free readers + locked writer.
 * Readers call xa_load() WITHOUT the lock.  Writer calls xa_store/xa_erase
 * with xa_lock held.  Because deferred frees are held until xa_rcu_drain(),
 * readers never hit use-after-free.
 */
#define RCU_READER_THREADS  4
#define RCU_OPS_PER_THREAD  2000

struct rcu_ctx {
    struct xarray *xa;
    int thread_id;
    int errors;
};

static void *rcu_lockfree_reader(void *arg)
{
    struct rcu_ctx *ctx = arg;
    struct xarray *xa = ctx->xa;
    ctx->errors = 0;

    for (int i = 0; i < RCU_OPS_PER_THREAD; i++) {
        uint64_t key = (uint64_t)(i % 500);
        xa_rcu_read_lock();
        void *v = xa_load(xa, key);         /* NO lock! */
        if (v != NULL && v != ENTRY(key))
            ctx->errors++;
        xa_rcu_read_unlock();
    }
    return NULL;
}

static void *rcu_locked_writer(void *arg)
{
    struct rcu_ctx *ctx = arg;
    struct xarray *xa = ctx->xa;
    ctx->errors = 0;

    for (int i = 0; i < RCU_OPS_PER_THREAD; i++) {
        uint64_t key = (uint64_t)(i % 500);
        xa_lock(xa);
        if (i % 3 == 0) {
            XA_STATE(xas, xa, key);
            xas_store(&xas, NULL);
        } else {
            XA_STATE(xas, xa, key);
            xas_store(&xas, ENTRY(key));
        }
        xa_unlock(xa);
    }
    return NULL;
}

static void test_rcu_lockfree_readers(void **state)
{
    (void)state;
    struct xarray xa;
    xa_init(&xa);

    /* Pre-populate so readers find data immediately. */
    for (uint64_t i = 0; i < 500; i++)
        xa_store(&xa, i, ENTRY(i), 0);

    /* Drain deferred frees from setup inserts. */
    xa_rcu_drain();

    pthread_t readers[RCU_READER_THREADS];
    struct rcu_ctx reader_ctxs[RCU_READER_THREADS];
    pthread_t writer;
    struct rcu_ctx writer_ctx = { .xa = &xa, .thread_id = 0, .errors = 0 };

    for (int i = 0; i < RCU_READER_THREADS; i++) {
        reader_ctxs[i] = (struct rcu_ctx){ .xa = &xa, .thread_id = i, .errors = 0 };
        pthread_create(&readers[i], NULL, rcu_lockfree_reader, &reader_ctxs[i]);
    }
    pthread_create(&writer, NULL, rcu_locked_writer, &writer_ctx);

    for (int i = 0; i < RCU_READER_THREADS; i++)
        pthread_join(readers[i], NULL);
    pthread_join(writer, NULL);

    int total_errors = writer_ctx.errors;
    for (int i = 0; i < RCU_READER_THREADS; i++)
        total_errors += reader_ctxs[i].errors;
    assert_int_equal(0, total_errors);

    xa_destroy(&xa);
    xa_rcu_drain();
}

/* ====================================================================== */
/*  Teardown: drain deferred frees after each test                         */
/* ====================================================================== */

static int rcu_teardown(void **state)
{
    (void)state;
    xa_rcu_drain();
    return 0;
}

/* ====================================================================== */
/*  Main — all 76 original tests + RCU-specific tests, each with teardown  */
/* ====================================================================== */

#define T(f) cmocka_unit_test_setup_teardown(f, NULL, rcu_teardown)

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* -- Original 76 tests (running under RCU mode) -- */
        T(test_empty_xarray),
        T(test_single_entry),
        T(test_store_overwrite),
        T(test_push_pop_pattern),
        T(test_erase_empty),
        T(test_erase_missing),
        T(test_store_null_erases),
        T(test_insert_sequential),
        T(test_insert_duplicate),
        T(test_sequence_cases),
        T(test_iteration_order),
        T(test_find_and_erase),
        T(test_find_operations),
        T(test_find_bounded),
        T(test_find_marked),
        T(test_marks_basic),
        T(test_marks_independent),
        T(test_marks_iteration),
        T(test_mark_missing_entry),
        T(test_marks_with_tree_depth),
        T(test_all_marks_on_entry),
        T(test_mark_survives_overwrite),
        T(test_mark_erase_stops_iteration),
        T(test_value_entries),
        T(test_value_entry_erase_overwrite),
        T(test_value_entry_edge_values),
        T(test_mixed_pointer_and_value),
        T(test_value_entries_with_marks),
        T(test_cursor_api),
        T(test_cursor_iteration),
        T(test_cursor_find_marked),
        T(test_cursor_mark_ops),
        T(test_cursor_store_multiple),
        T(test_cursor_reposition),
        T(test_full_node),
        T(test_node_boundary_crossing),
        T(test_multi_level_tree),
        T(test_power_of_64_indices),
        T(test_erase_shrinks_tree),
        T(test_large_index),
        T(test_sparse_entries),
        T(test_extreme_indices),
        T(test_delete_balancing),
        T(test_scale),
        T(test_reinsert_after_erase),
        T(test_bulk_reinsert_cycle),
        T(test_reverse_erase),
        T(test_destroy_with_entries),
        T(test_destroy_empty),
        T(test_uint64_max_index),
        T(test_index0_head_marks),
        T(test_store_index0_into_deep_tree),
        T(test_find_empty),
        T(test_find_after_uint64_max),
        T(test_find_after_at_max),
        T(test_find_max_zero),
        T(test_marked_iteration_empty_and_no_marks),
        T(test_double_erase_same_index),
        T(test_double_destroy_with_entries),
        T(test_sibling_entries),
        T(test_clear_mark_without_mark),
        T(test_marks_across_levels_partial_erase),
        T(test_get_mark_on_empty_index),
        T(test_init_flags),
        T(test_invalid_mark_value),
        T(test_store_null_nonexistent),
        T(test_cursor_error_state),
        T(test_store_internal_entries),
        T(test_mark_overwrite_head_vs_node),
        T(test_stress_random_churn),
        T(test_stress_sparse_random),
        T(test_stress_grow_shrink_cycles),
        T(test_stress_random_marks),
        T(test_stress_node_churn),
        T(test_stress_interleaved_ops),
        T(test_stress_cursor_api),

        /* P0: Correctness edge cases */
        T(test_head_marks_survive_expand),
        T(test_init_flags_high_bits),

        /* P1: Sibling subsystem */
        T(test_sibling_erase),
        T(test_sibling_resize_shrink),
        T(test_sibling_resize_grow),
        T(test_sibling_at_chunk_boundary),
        T(test_sibling_marks),

        /* P2: Find/iteration boundaries */
        T(test_find_after_reaches_uint64_max),
        T(test_walk_next_rightmost_slots),
        T(test_find_max_at_intermediate_level),

        /* P3: Structural / cleanup */
        T(test_shrink_nonzero_slot),
        T(test_destroy_deep_tree),
        T(test_cascading_delete_node),
        T(test_mutation_during_iteration),

        /* P4: Defensive / minor */
        T(test_value_entry_near_sentinel),
        T(test_cursor_find_marked_head),
        T(test_sibling_store_on_head),
        T(test_multi_level_expand),

        /* -- RCU-specific tests -- */
        T(test_rcu_deferred_accounting),
        T(test_rcu_destroy_defers_reclamation),
        T(test_rcu_lockfree_readers),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
