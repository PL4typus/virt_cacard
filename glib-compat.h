/*
 * GLIB Compatibility Functions
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Michael Tokarev   <mjt@tls.msk.ru>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef GLIB_COMPAT_H
#define GLIB_COMPAT_H

#include <glib.h>

#if !GLIB_CHECK_VERSION(2, 31, 0)
/* before glib-2.31, GMutex and GCond was dynamic-only (there was a separate
 * GStaticMutex, but it didn't work with condition variables).
 *
 * Our implementation uses GOnce to fake a static implementation that does
 * not require separate initialization.
 * We need to rename the types to avoid passing our CompatGMutex/CompatGCond
 * by mistake to a function that expects GMutex/GCond.  However, for ease
 * of use we keep the GLib function names.  GLib uses macros for the
 * implementation, we use inline functions instead and undefine the macros.
 */

typedef struct CompatGMutex {
    GOnce once;
} CompatGMutex;

typedef struct CompatGCond {
    GOnce once;
} CompatGCond;

static inline gpointer do_g_mutex_new(gpointer unused)
{
    return (gpointer) g_mutex_new();
}

static inline void g_mutex_init(CompatGMutex *mutex)
{
    mutex->once = (GOnce) G_ONCE_INIT;
}

static inline void g_mutex_clear(CompatGMutex *mutex)
{
    g_assert(mutex->once.status != G_ONCE_STATUS_PROGRESS);
    if (mutex->once.retval) {
        g_mutex_free((GMutex *) mutex->once.retval);
    }
    mutex->once = (GOnce) G_ONCE_INIT;
}

static inline void (g_mutex_lock)(CompatGMutex *mutex)
{
    g_once(&mutex->once, do_g_mutex_new, NULL);
    g_mutex_lock((GMutex *) mutex->once.retval);
}
#undef g_mutex_lock

static inline gboolean (g_mutex_trylock)(CompatGMutex *mutex)
{
    g_once(&mutex->once, do_g_mutex_new, NULL);
    return g_mutex_trylock((GMutex *) mutex->once.retval);
}
#undef g_mutex_trylock


static inline void (g_mutex_unlock)(CompatGMutex *mutex)
{
    g_mutex_unlock((GMutex *) mutex->once.retval);
}
#undef g_mutex_unlock

static inline gpointer do_g_cond_new(gpointer unused)
{
    return (gpointer) g_cond_new();
}

static inline void g_cond_init(CompatGCond *cond)
{
    cond->once = (GOnce) G_ONCE_INIT;
}

static inline void g_cond_clear(CompatGCond *cond)
{
    g_assert(cond->once.status != G_ONCE_STATUS_PROGRESS);
    if (cond->once.retval) {
        g_cond_free((GCond *) cond->once.retval);
    }
    cond->once = (GOnce) G_ONCE_INIT;
}

static inline void (g_cond_wait)(CompatGCond *cond, CompatGMutex *mutex)
{
    g_assert(mutex->once.status != G_ONCE_STATUS_PROGRESS);
    g_once(&cond->once, do_g_cond_new, NULL);
    g_cond_wait((GCond *) cond->once.retval, (GMutex *) mutex->once.retval);
}
#undef g_cond_wait

static inline void (g_cond_broadcast)(CompatGCond *cond)
{
    g_once(&cond->once, do_g_cond_new, NULL);
    g_cond_broadcast((GCond *) cond->once.retval);
}
#undef g_cond_broadcast

static inline void (g_cond_signal)(CompatGCond *cond)
{
    g_once(&cond->once, do_g_cond_new, NULL);
    g_cond_signal((GCond *) cond->once.retval);
}
#undef g_cond_signal


/* before 2.31 there was no g_thread_new() */
static inline GThread *g_thread_new(const char *name,
                                    GThreadFunc func, gpointer data)
{
    GThread *thread = g_thread_create(func, data, TRUE, NULL);
    if (!thread) {
        g_error("creating thread");
    }
    return thread;
}
#else
#define CompatGMutex GMutex
#define CompatGCond GCond
#endif /* glib 2.31 */

#endif
