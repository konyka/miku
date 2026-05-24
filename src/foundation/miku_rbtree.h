#ifndef MIKU_RBTREE_H
#define MIKU_RBTREE_H

#include "miku_common.h"

typedef enum { MK_RB_BLACK = 0, MK_RB_RED = 1 } miku_rb_color_t;

typedef struct miku_rbnode_s {
    struct miku_rbnode_s *left;
    struct miku_rbnode_s *right;
    struct miku_rbnode_s *parent;
    miku_rb_color_t       color;
} miku_rbnode_t;

typedef int (*miku_rb_cmp_fn)(const miku_rbnode_t *a, const miku_rbnode_t *b);

typedef struct {
    miku_rbnode_t  sentinel;
    miku_rbnode_t *root;
    miku_rb_cmp_fn cmp;
} miku_rbtree_t;

MIKU_API void          miku_rbtree_init(miku_rbtree_t *tree, miku_rb_cmp_fn cmp);
MIKU_API void          miku_rbtree_insert(miku_rbtree_t *tree, miku_rbnode_t *node);
MIKU_API void          miku_rbtree_delete(miku_rbtree_t *tree, miku_rbnode_t *node);
MIKU_API miku_rbnode_t *miku_rbtree_first(const miku_rbtree_t *tree);
MIKU_API miku_rbnode_t *miku_rbtree_next(const miku_rbtree_t *tree, const miku_rbnode_t *node);
MIKU_API bool          miku_rbtree_empty(const miku_rbtree_t *tree);

#endif
