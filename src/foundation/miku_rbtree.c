#include "miku_rbtree.h"
#include <stddef.h>

static miku_rbnode_t *rb_sentinel(miku_rbtree_t *t) { return &t->sentinel; }

static void rb_left_rotate(miku_rbtree_t *t, miku_rbnode_t *x) {
    miku_rbnode_t *y = x->right;
    x->right = y->left;
    if (y->left != rb_sentinel(t)) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == rb_sentinel(t)) t->root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}

static void rb_right_rotate(miku_rbtree_t *t, miku_rbnode_t *x) {
    miku_rbnode_t *y = x->left;
    x->left = y->right;
    if (y->right != rb_sentinel(t)) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == rb_sentinel(t)) t->root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;
    y->right = x;
    x->parent = y;
}

static void rb_insert_fixup(miku_rbtree_t *t, miku_rbnode_t *z) {
    while (z->parent->color == MK_RB_RED) {
        if (z->parent == z->parent->parent->left) {
            miku_rbnode_t *y = z->parent->parent->right;
            if (y->color == MK_RB_RED) {
                z->parent->color = MK_RB_BLACK;
                y->color = MK_RB_BLACK;
                z->parent->parent->color = MK_RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) { z = z->parent; rb_left_rotate(t, z); }
                z->parent->color = MK_RB_BLACK;
                z->parent->parent->color = MK_RB_RED;
                rb_right_rotate(t, z->parent->parent);
            }
        } else {
            miku_rbnode_t *y = z->parent->parent->left;
            if (y->color == MK_RB_RED) {
                z->parent->color = MK_RB_BLACK;
                y->color = MK_RB_BLACK;
                z->parent->parent->color = MK_RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) { z = z->parent; rb_right_rotate(t, z); }
                z->parent->color = MK_RB_BLACK;
                z->parent->parent->color = MK_RB_RED;
                rb_left_rotate(t, z->parent->parent);
            }
        }
    }
    t->root->color = MK_RB_BLACK;
}

static void rb_transplant(miku_rbtree_t *t, miku_rbnode_t *u, miku_rbnode_t *v) {
    if (u->parent == rb_sentinel(t)) t->root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else u->parent->right = v;
    v->parent = u->parent;
}

static miku_rbnode_t *rb_min(miku_rbtree_t *t, miku_rbnode_t *x) {
    while (x->left != rb_sentinel(t)) x = x->left;
    return x;
}

static void rb_delete_fixup(miku_rbtree_t *t, miku_rbnode_t *x) {
    while (x != t->root && x->color == MK_RB_BLACK) {
        if (x == x->parent->left) {
            miku_rbnode_t *w = x->parent->right;
            if (w->color == MK_RB_RED) {
                w->color = MK_RB_BLACK;
                x->parent->color = MK_RB_RED;
                rb_left_rotate(t, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == MK_RB_BLACK && w->right->color == MK_RB_BLACK) {
                w->color = MK_RB_RED;
                x = x->parent;
            } else {
                if (w->right->color == MK_RB_BLACK) {
                    w->left->color = MK_RB_BLACK;
                    w->color = MK_RB_RED;
                    rb_right_rotate(t, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = MK_RB_BLACK;
                w->right->color = MK_RB_BLACK;
                rb_left_rotate(t, x->parent);
                x = t->root;
            }
        } else {
            miku_rbnode_t *w = x->parent->left;
            if (w->color == MK_RB_RED) {
                w->color = MK_RB_BLACK;
                x->parent->color = MK_RB_RED;
                rb_right_rotate(t, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == MK_RB_BLACK && w->left->color == MK_RB_BLACK) {
                w->color = MK_RB_RED;
                x = x->parent;
            } else {
                if (w->left->color == MK_RB_BLACK) {
                    w->right->color = MK_RB_BLACK;
                    w->color = MK_RB_RED;
                    rb_left_rotate(t, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = MK_RB_BLACK;
                w->left->color = MK_RB_BLACK;
                rb_right_rotate(t, x->parent);
                x = t->root;
            }
        }
    }
    x->color = MK_RB_BLACK;
}

void miku_rbtree_init(miku_rbtree_t *tree, miku_rb_cmp_fn cmp) {
    tree->sentinel.left = &tree->sentinel;
    tree->sentinel.right = &tree->sentinel;
    tree->sentinel.parent = &tree->sentinel;
    tree->sentinel.color = MK_RB_BLACK;
    tree->root = &tree->sentinel;
    tree->cmp = cmp;
}

void miku_rbtree_insert(miku_rbtree_t *tree, miku_rbnode_t *node) {
    miku_rbnode_t *y = rb_sentinel(tree);
    miku_rbnode_t *x = tree->root;
    while (x != rb_sentinel(tree)) {
        y = x;
        x = (tree->cmp(node, x) < 0) ? x->left : x->right;
    }
    node->parent = y;
    if (y == rb_sentinel(tree)) tree->root = node;
    else if (tree->cmp(node, y) < 0) y->left = node;
    else y->right = node;
    node->left = rb_sentinel(tree);
    node->right = rb_sentinel(tree);
    node->color = MK_RB_RED;
    rb_insert_fixup(tree, node);
}

void miku_rbtree_delete(miku_rbtree_t *tree, miku_rbnode_t *z) {
    miku_rbnode_t *y = z, *x;
    miku_rb_color_t y_orig = y->color;
    if (z->left == rb_sentinel(tree)) {
        x = z->right;
        rb_transplant(tree, z, z->right);
    } else if (z->right == rb_sentinel(tree)) {
        x = z->left;
        rb_transplant(tree, z, z->left);
    } else {
        y = rb_min(tree, z->right);
        y_orig = y->color;
        x = y->right;
        if (y->parent == z) x->parent = y;
        else { rb_transplant(tree, y, y->right); y->right = z->right; y->right->parent = y; }
        rb_transplant(tree, z, y);
        y->left = z->left; y->left->parent = y; y->color = z->color;
    }
    if (y_orig == MK_RB_BLACK) rb_delete_fixup(tree, x);
}

miku_rbnode_t *miku_rbtree_first(const miku_rbtree_t *tree) {
    if (tree->root == rb_sentinel((miku_rbtree_t *)tree)) return NULL;
    return rb_min((miku_rbtree_t *)tree, tree->root);
}

miku_rbnode_t *miku_rbtree_next(const miku_rbtree_t *tree, const miku_rbnode_t *node) {
    miku_rbnode_t *n = (miku_rbnode_t *)node;
    miku_rbtree_t *t = (miku_rbtree_t *)tree;
    if (n->right != rb_sentinel(t)) return rb_min(t, n->right);
    miku_rbnode_t *p = n->parent;
    while (p != rb_sentinel(t) && n == p->right) { n = p; p = p->parent; }
    return (p == rb_sentinel(t)) ? NULL : p;
}

bool miku_rbtree_empty(const miku_rbtree_t *tree) {
    return tree->root == &tree->sentinel;
}
