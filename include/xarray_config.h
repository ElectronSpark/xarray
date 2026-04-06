/**
 * @file xarray_config.h
 * @brief XArray build-time configuration and pluggable interfaces.
 *
 * This header provides:
 *  1. Build-time feature toggles (locking, RCU).
 *  2. Pluggable memory allocation interface.
 *  3. Pluggable lock and RCU interfaces (when enabled).
 *
 * Users can override allocation by defining XA_CUSTOM_ALLOC before
 * including this header and providing xa_alloc_fn / xa_free_fn.
 *
 * Feature macros:
 *   XA_CONFIG_LOCK  — Enable internal spinlock protection on write ops.
 *   XA_CONFIG_RCU   — Enable RCU read-side lock-free lookups.
 */

#ifndef XARRAY_CONFIG_H
#define XARRAY_CONFIG_H

#include <stddef.h>

/* ====================================================================== */
/*  Feature toggles                                                        */
/* ====================================================================== */

/*
 * Define XA_CONFIG_LOCK to include a lock in struct xarray and have the
 * simple API (xa_store, xa_erase, etc.) acquire it automatically.
 *
 * Define XA_CONFIG_RCU to wrap xa_load / xa_find in RCU read-side sections
 * and defer node freeing via RCU callbacks.
 *
 * Both can be defined independently, though XA_CONFIG_RCU without
 * XA_CONFIG_LOCK is unusual.
 */

/* Uncomment or define via -D to enable:
 * #define XA_CONFIG_LOCK
 * #define XA_CONFIG_RCU
 */

/* ====================================================================== */
/*  Memory allocation interface                                            */
/* ====================================================================== */

/**
 * Users may define XA_CUSTOM_ALLOC before including any xarray header
 * and provide their own allocation functions:
 *
 *   void *xa_alloc_fn(size_t size);
 *   void  xa_free_fn(void *ptr);
 *
 * If XA_CUSTOM_ALLOC is not defined, stdlib malloc/free are used.
 */

#ifndef XA_CUSTOM_ALLOC

#include <stdlib.h>
#include <string.h>

static inline void *xa_alloc_fn(size_t size)
{
    void *p = malloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

static inline void xa_free_fn(void *ptr)
{
    free(ptr);
}

#else /* XA_CUSTOM_ALLOC */

/*
 * User must provide:
 *   void *xa_alloc_fn(size_t size);   — returns zeroed memory or NULL
 *   void  xa_free_fn(void *ptr);
 */
extern void *xa_alloc_fn(size_t size);
extern void  xa_free_fn(void *ptr);

#endif /* XA_CUSTOM_ALLOC */

/* ====================================================================== */
/*  Lock interface                                                         */
/* ====================================================================== */

#ifdef XA_CONFIG_LOCK

/*
 * When locking is enabled, the user must provide an implementation or
 * use the defaults below.  Define XA_CUSTOM_LOCK to override.
 *
 * Required types and macros:
 *   xa_lock_t                        — lock type
 *   XA_LOCK_INITIALIZER(name)        — static initialiser
 *   xa_lock_init(lock)               — runtime initialisation
 *   xa_spin_lock(lock)               — acquire
 *   xa_spin_unlock(lock)             — release
 */

#ifndef XA_CUSTOM_LOCK

#include <pthread.h>

typedef pthread_mutex_t xa_lock_t;

#define XA_LOCK_INITIALIZER(name)  PTHREAD_MUTEX_INITIALIZER

static inline void xa_lock_init(xa_lock_t *lock)
{
    pthread_mutex_init(lock, NULL);
}

static inline void xa_spin_lock(xa_lock_t *lock)
{
    pthread_mutex_lock(lock);
}

static inline void xa_spin_unlock(xa_lock_t *lock)
{
    pthread_mutex_unlock(lock);
}

#endif /* XA_CUSTOM_LOCK */

#endif /* XA_CONFIG_LOCK */

/* ====================================================================== */
/*  RCU interface                                                          */
/* ====================================================================== */

#ifdef XA_CONFIG_RCU

/*
 * When RCU is enabled, the user must provide an implementation or
 * use the defaults below.  Define XA_CUSTOM_RCU to override.
 *
 * Required functions:
 *   xa_rcu_read_lock()
 *   xa_rcu_read_unlock()
 *   xa_call_rcu(callback, data)      — schedule callback(data) after grace period
 */

#ifndef XA_CUSTOM_RCU

/*
 * Default stub RCU: no actual deferral — calls the callback immediately.
 * This is safe for single-threaded use or when no concurrent readers exist.
 */
static inline void xa_rcu_read_lock(void) {}
static inline void xa_rcu_read_unlock(void) {}

typedef void (*xa_rcu_callback_t)(void *);

static inline void xa_call_rcu(xa_rcu_callback_t cb, void *data)
{
    cb(data);
}

#else /* XA_CUSTOM_RCU */

extern void xa_rcu_read_lock(void);
extern void xa_rcu_read_unlock(void);

typedef void (*xa_rcu_callback_t)(void *);
extern void xa_call_rcu(xa_rcu_callback_t cb, void *data);

#endif /* XA_CUSTOM_RCU */

#endif /* XA_CONFIG_RCU */

/* ====================================================================== */
/*  Memory barrier primitives                                              */
/* ====================================================================== */

/*
 * Three barriers modelled after the Linux kernel's SMP barriers:
 *
 *   xa_smp_rmb() — read  barrier (load-load ordering)
 *   xa_smp_wmb() — write barrier (store-store ordering)
 *   xa_smp_mb()  — full  barrier (load/store-load/store ordering)
 *
 * Users may define XA_CUSTOM_BARRIERS and provide their own.
 * Otherwise architecture-specific implementations are selected
 * automatically, falling back to __atomic_thread_fence.
 */

#ifndef XA_CUSTOM_BARRIERS

#if defined(__x86_64__) || defined(__i386__)

/*
 * x86 has a strong (TSO) memory model: loads are never reordered with
 * loads, stores are never reordered with stores.  A compiler barrier
 * is sufficient for rmb/wmb.
 */
#define xa_smp_rmb()  __asm__ volatile("" ::: "memory")
#define xa_smp_wmb()  __asm__ volatile("" ::: "memory")
#define xa_smp_mb()   __asm__ volatile("mfence" ::: "memory")

#elif defined(__aarch64__)

/*
 * ARM64: lightweight inner-shareable barriers.
 *   dmb ishld — load-load  (cheaper than full dmb ish)
 *   dmb ishst — store-store
 *   dmb ish   — full
 */
#define xa_smp_rmb()  __asm__ volatile("dmb ishld" ::: "memory")
#define xa_smp_wmb()  __asm__ volatile("dmb ishst" ::: "memory")
#define xa_smp_mb()   __asm__ volatile("dmb ish"   ::: "memory")

#elif defined(__riscv)

/*
 * RISC-V: fine-grained fence instruction.
 *   fence r,r  — load-load
 *   fence w,w  — store-store
 *   fence rw,rw — full
 */
#define xa_smp_rmb()  __asm__ volatile("fence r,r"   ::: "memory")
#define xa_smp_wmb()  __asm__ volatile("fence w,w"   ::: "memory")
#define xa_smp_mb()   __asm__ volatile("fence rw,rw" ::: "memory")

#else

/* Generic fallback using GCC/Clang atomic fences. */
#define xa_smp_rmb()  __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define xa_smp_wmb()  __atomic_thread_fence(__ATOMIC_RELEASE)
#define xa_smp_mb()   __atomic_thread_fence(__ATOMIC_SEQ_CST)

#endif /* arch detection */

#endif /* XA_CUSTOM_BARRIERS */

/* ====================================================================== */
/*  Compiler helpers                                                       */
/* ====================================================================== */

/*
 * READ_ONCE / WRITE_ONCE — prevent the compiler from optimising away or
 * reordering individual accesses.  Used for RCU-safe reads of fields that
 * may be concurrently written (e.g. node->count, xa_head).
 */
#define READ_ONCE(x)       (*(const volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, val) do { *(volatile __typeof__(x) *)&(x) = (val); } while (0)

/* ====================================================================== */
/*  Slot / flag access with memory ordering                                */
/* ====================================================================== */

/*
 * When RCU is enabled, readers are lock-free so stores by the (locked)
 * writer must be visible to them in the correct order:
 *
 *   - Reads:  volatile load + xa_smp_rmb()   (load-load)
 *             Reader loads a slot, then follows the pointer.  The read
 *             barrier ensures the data behind the pointer is observed
 *             no earlier than the pointer itself.
 *
 *   - Writes: xa_smp_wmb() + volatile store  (store-store)
 *             Writer fills a node, then publishes its pointer.  The write
 *             barrier ensures node contents are committed before the
 *             pointer becomes visible to readers.
 *
 *   - RMW on flags: plain load-modify + xa_smp_wmb() + volatile store.
 *             Safe because writers are serialised by xa_lock.
 *
 * Without RCU everything is plain access (no fences needed).
 */
#ifdef XA_CONFIG_RCU

static inline void *xa_slot_load(void **slot)
{
    void *p = *(void *volatile *)slot;
    xa_smp_rmb();
    return p;
}

static inline void xa_slot_store(void **slot, void *entry)
{
    xa_smp_wmb();
    *(void *volatile *)slot = entry;
}

static inline unsigned int xa_flags_load(const unsigned int *flags)
{
    unsigned int v = *(const volatile unsigned int *)flags;
    xa_smp_rmb();
    return v;
}

static inline void xa_flags_or(unsigned int *flags, unsigned int bits)
{
    unsigned int v = *flags;
    v |= bits;
    xa_smp_wmb();
    *(volatile unsigned int *)flags = v;
}

static inline void xa_flags_and(unsigned int *flags, unsigned int bits)
{
    unsigned int v = *flags;
    v &= bits;
    xa_smp_wmb();
    *(volatile unsigned int *)flags = v;
}

#else /* !XA_CONFIG_RCU */

static inline void *xa_slot_load(void **slot)
{
    return *slot;
}

static inline void xa_slot_store(void **slot, void *entry)
{
    *slot = entry;
}

static inline unsigned int xa_flags_load(const unsigned int *flags)
{
    return *flags;
}

static inline void xa_flags_or(unsigned int *flags, unsigned int bits)
{
    *flags |= bits;
}

static inline void xa_flags_and(unsigned int *flags, unsigned int bits)
{
    *flags &= bits;
}

#endif /* XA_CONFIG_RCU */

#endif /* XARRAY_CONFIG_H */
