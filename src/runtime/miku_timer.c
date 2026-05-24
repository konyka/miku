#include "miku_timer.h"
#include "miku_rbtree.h"
#include "miku_thread.h"
#include <stdlib.h>

typedef struct {
    miku_rbnode_t  rbnode;
    uint64_t       id;
    int64_t        deadline_ms;
    miku_timer_fn  fn;
    void          *arg;
    bool           cancelled;
} timer_entry_t;

struct miku_timer_s {
    miku_rbtree_t   tree;
    miku_mutex_t    lock;
    uint64_t        next_id;
};

static int timer_cmp(const miku_rbnode_t *a, const miku_rbnode_t *b) {
    const timer_entry_t *ea = MIKU_CONTAINER_OF(a, timer_entry_t, rbnode);
    const timer_entry_t *eb = MIKU_CONTAINER_OF(b, timer_entry_t, rbnode);
    if (ea->deadline_ms < eb->deadline_ms) return -1;
    if (ea->deadline_ms > eb->deadline_ms) return 1;
    if (ea->id < eb->id) return -1;
    if (ea->id > eb->id) return 1;
    return 0;
}

miku_timer_t *miku_timer_create(void) {
    miku_timer_t *tm = (miku_timer_t *)calloc(1, sizeof(*tm));
    if (!tm) return NULL;
    miku_rbtree_init(&tm->tree, timer_cmp);
    miku_mutex_init(&tm->lock);
    return tm;
}

int miku_timer_add(miku_timer_t *tm, int64_t deadline_ms,
                    miku_timer_fn fn, void *arg, uint64_t *timer_id) {
    if (!tm || !fn) return -1;
    timer_entry_t *e = (timer_entry_t *)calloc(1, sizeof(*e));
    if (!e) return -1;
    e->deadline_ms = deadline_ms;
    e->fn = fn;
    e->arg = arg;

    miku_mutex_lock(&tm->lock);
    e->id = ++tm->next_id;
    miku_rbtree_insert(&tm->tree, &e->rbnode);
    miku_mutex_unlock(&tm->lock);

    if (timer_id) *timer_id = e->id;
    return 0;
}

static timer_entry_t *find_by_id(miku_timer_t *tm, uint64_t id) {
    miku_rbnode_t *n = miku_rbtree_first(&tm->tree);
    while (n) {
        timer_entry_t *e = MIKU_CONTAINER_OF(n, timer_entry_t, rbnode);
        if (e->id == id) return e;
        n = miku_rbtree_next(&tm->tree, n);
    }
    return NULL;
}

int miku_timer_cancel(miku_timer_t *tm, uint64_t timer_id) {
    if (!tm) return -1;
    miku_mutex_lock(&tm->lock);
    timer_entry_t *e = find_by_id(tm, timer_id);
    if (e) { e->cancelled = true; }
    miku_mutex_unlock(&tm->lock);
    return e ? 0 : -1;
}

int miku_timer_process(miku_timer_t *tm) {
    if (!tm) return 0;
    int64_t now = miku_timestamp_ms();
    int fired = 0;

    miku_mutex_lock(&tm->lock);
    while (!miku_rbtree_empty(&tm->tree)) {
        miku_rbnode_t *n = miku_rbtree_first(&tm->tree);
        timer_entry_t *e = MIKU_CONTAINER_OF(n, timer_entry_t, rbnode);
        if (e->deadline_ms > now) break;
        miku_rbtree_delete(&tm->tree, n);
        miku_mutex_unlock(&tm->lock);

        if (!e->cancelled) { e->fn(e->arg); fired++; }
        free(e);

        miku_mutex_lock(&tm->lock);
    }
    miku_mutex_unlock(&tm->lock);
    return fired;
}

int64_t miku_timer_next_deadline(const miku_timer_t *tm) {
    if (!tm || miku_rbtree_empty(&tm->tree)) return -1;
    miku_rbnode_t *n = miku_rbtree_first((miku_rbtree_t *)&tm->tree);
    timer_entry_t *e = MIKU_CONTAINER_OF(n, timer_entry_t, rbnode);
    return e->deadline_ms;
}

void miku_timer_destroy(miku_timer_t *tm) {
    if (!tm) return;
    while (!miku_rbtree_empty(&tm->tree)) {
        miku_rbnode_t *n = miku_rbtree_first(&tm->tree);
        timer_entry_t *e = MIKU_CONTAINER_OF(n, timer_entry_t, rbnode);
        miku_rbtree_delete(&tm->tree, n);
        free(e);
    }
    miku_mutex_destroy(&tm->lock);
    free(tm);
}
