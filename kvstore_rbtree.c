#include <string.h>

#include "kvstore.h"

#if ENABLE_RBTREE_KVENGINE

/* ============================================================================
 *  红黑树引擎（自平衡二叉搜索树，标准 CLRS 实现）
 * ----------------------------------------------------------------------------
 *  红黑树五条性质（靠它保证树高 O(log n)）：
 *    1. 每个节点非红即黑。
 *    2. 根节点是黑色。
 *    3. 每个叶子(NIL)是黑色。  —— 本实现用一个哨兵节点 nil 代替所有 NULL 叶子。
 *    4. 红节点的两个孩子都是黑色（不能有连续红节点）。
 *    5. 从任一节点到其所有叶子的路径，黑色节点数相同（黑高一致）。
 *  => 由 4、5 可推出：最长路径 <= 2 * 最短路径，所以树是"近似平衡"的。
 *
 *  哨兵 nil 的作用：把所有空指针统一成一个黑色节点，旋转/修复时不必到处判 NULL，
 *  代码大幅简化（nil 的 parent 也可被临时写入，delete_fixup 依赖这一点）。
 *
 *  复杂度：增删查均 O(log n)，且天然有序（中序遍历即升序），支持范围查询。
 *
 *  语义约定（与 array/hash 引擎一致）：
 *    set: 0 成功 / 1 key 已存在(不覆盖) / -1 出错
 *    get: value 指针 / NULL
 *    del/mod: 0 成功 / 1 不存在 / -1 出错
 * ========================================================================== */
#define RBTREE_RED 0
#define RBTREE_BLACK 1

struct rbtree_node {
    int color;                  /* RBTREE_RED 或 RBTREE_BLACK */
    struct rbtree_node *left;
    struct rbtree_node *right;
    struct rbtree_node *parent; /* 指向父节点，旋转/修复时要向上回溯 */
    char *key;
    char *value;
};

struct rbtree {
    struct rbtree_node *root;
    struct rbtree_node *nil;    /* 哨兵：代表所有空叶子，恒为黑色 */
};

static struct rbtree rb_tree = {0};

static char *kvstore_strdup(const char *src) {
    char *dst = NULL;

    if (src == NULL) {
        return NULL;
    }

    dst = kvstore_malloc(strlen(src) + 1);
    if (dst == NULL) {
        return NULL;
    }

    strcpy(dst, src);
    return dst;
}

static struct rbtree_node *rbtree_search_node(char *key) {
    struct rbtree_node *node = NULL;

    if (key == NULL || rb_tree.nil == NULL) {
        return NULL;
    }

    node = rb_tree.root;
    while (node != rb_tree.nil) {
        int cmp = strcmp(key, node->key);
        if (cmp == 0) {
            return node;
        } else if (cmp < 0) {
            node = node->left;
        } else {
            node = node->right;
        }
    }

    return NULL;
}

/* 左旋：以 x 为支点，把它的右孩子 y 转上来当父亲，x 变成 y 的左孩子。
 * y 原来的左子树 (y->left) 改挂到 x 的右边。旋转保持二叉搜索树的有序性不变，
 * 只调整高度/结构，是插入和删除修复时重新平衡的基础动作。右旋是它的镜像。
 *
 *      x              y
 *       \    左旋     / \
 *        y   ==>     x   c
 *       / \           \
 *      b   c           b
 */
static void rbtree_left_rotate(struct rbtree_node *x) {
    struct rbtree_node *y = x->right;

    x->right = y->left;
    if (y->left != rb_tree.nil) {
        y->left->parent = x;
    }

    y->parent = x->parent;
    if (x->parent == rb_tree.nil) {
        rb_tree.root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }

    y->left = x;
    x->parent = y;
}

static void rbtree_right_rotate(struct rbtree_node *y) {
    struct rbtree_node *x = y->left;

    y->left = x->right;
    if (x->right != rb_tree.nil) {
        x->right->parent = y;
    }

    x->parent = y->parent;
    if (y->parent == rb_tree.nil) {
        rb_tree.root = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }

    x->right = y;
    y->parent = x;
}

/* 插入修复：新节点 z 一律先染红（插红不破坏黑高性质5，只可能破坏性质4）。
 * 若父节点也是红 -> 出现"连续红"，需修复。以"父亲是祖父的左孩子"为例，看叔叔 y：
 *   case1 叔叔红：父和叔都变黑、祖父变红，把问题上移到祖父继续看（z = 祖父）。
 *   case2 叔叔黑且 z 在内侧(右)：先对父左旋，转成 case3。
 *   case3 叔叔黑且 z 在外侧(左)：父变黑、祖父变红，对祖父右旋，结束。
 * "父亲是右孩子"的分支是上面的左右镜像。最后强制根为黑（性质2）。
 * 插入最多 2 次旋转。 */
static void rbtree_insert_fixup(struct rbtree_node *z) {
    while (z->parent->color == RBTREE_RED) {
        if (z->parent == z->parent->parent->left) {
            struct rbtree_node *y = z->parent->parent->right;  /* 叔叔节点 */

            if (y->color == RBTREE_RED) {
                /* case1：叔叔红，变色后把矛盾上移 */
                z->parent->color = RBTREE_BLACK;
                y->color = RBTREE_BLACK;
                z->parent->parent->color = RBTREE_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    /* case2：内侧，先左旋转成外侧(case3) */
                    z = z->parent;
                    rbtree_left_rotate(z);
                }
                /* case3：外侧，变色 + 右旋祖父 */
                z->parent->color = RBTREE_BLACK;
                z->parent->parent->color = RBTREE_RED;
                rbtree_right_rotate(z->parent->parent);
            }
        } else {  /* 镜像：父亲是祖父的右孩子 */
            struct rbtree_node *y = z->parent->parent->left;

            if (y->color == RBTREE_RED) {
                z->parent->color = RBTREE_BLACK;
                y->color = RBTREE_BLACK;
                z->parent->parent->color = RBTREE_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rbtree_right_rotate(z);
                }
                z->parent->color = RBTREE_BLACK;
                z->parent->parent->color = RBTREE_RED;
                rbtree_left_rotate(z->parent->parent);
            }
        }
    }

    rb_tree.root->color = RBTREE_BLACK;
}

static struct rbtree_node *rbtree_minimum(struct rbtree_node *node) {
    while (node->left != rb_tree.nil) {
        node = node->left;
    }

    return node;
}

/* transplant：用子树 v 顶替子树 u 的位置（只改 u 父亲的指向 + v 的 parent）。
 * 注意 v 可能是 nil，这里仍写 v->parent —— 这正是哨兵的用处：让 delete 后
 * delete_fixup 能从 nil 往上回溯。删除时用它把后继/独子接到被删节点的位置。 */
static void rbtree_transplant(struct rbtree_node *u, struct rbtree_node *v) {
    if (u->parent == rb_tree.nil) {
        rb_tree.root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }

    v->parent = u->parent;
}

/* 删除修复：删掉一个黑节点会让经过它的路径少一个黑，破坏性质5。
 * x 是顶替上来的节点，可看作"额外背负一重黑"。围绕兄弟 w 分四种情况修复
 * （以 x 是左孩子为例，右孩子为镜像）：
 *   case1 兄弟红：旋转+变色，把兄弟变黑，转成 case2/3/4。
 *   case2 兄弟黑且两个孩子都黑：兄弟变红，把"额外黑"上移到父亲(x=父亲)。
 *   case3 兄弟黑、近侄黑远侄红的反例(远侄黑)：旋转兄弟，转成 case4。
 *   case4 兄弟黑且远侄红：变色 + 旋转父亲，彻底补上黑高，结束(x=root)。
 * 删除最多 3 次旋转。最后把 x 染黑收尾。 */
static void rbtree_delete_fixup(struct rbtree_node *x) {
    while (x != rb_tree.root && x->color == RBTREE_BLACK) {
        if (x == x->parent->left) {
            struct rbtree_node *w = x->parent->right;

            if (w->color == RBTREE_RED) {
                w->color = RBTREE_BLACK;
                x->parent->color = RBTREE_RED;
                rbtree_left_rotate(x->parent);
                w = x->parent->right;
            }

            if (w->left->color == RBTREE_BLACK && w->right->color == RBTREE_BLACK) {
                w->color = RBTREE_RED;
                x = x->parent;
            } else {
                if (w->right->color == RBTREE_BLACK) {
                    w->left->color = RBTREE_BLACK;
                    w->color = RBTREE_RED;
                    rbtree_right_rotate(w);
                    w = x->parent->right;
                }

                w->color = x->parent->color;
                x->parent->color = RBTREE_BLACK;
                w->right->color = RBTREE_BLACK;
                rbtree_left_rotate(x->parent);
                x = rb_tree.root;
            }
        } else {
            struct rbtree_node *w = x->parent->left;

            if (w->color == RBTREE_RED) {
                w->color = RBTREE_BLACK;
                x->parent->color = RBTREE_RED;
                rbtree_right_rotate(x->parent);
                w = x->parent->left;
            }

            if (w->right->color == RBTREE_BLACK && w->left->color == RBTREE_BLACK) {
                w->color = RBTREE_RED;
                x = x->parent;
            } else {
                if (w->left->color == RBTREE_BLACK) {
                    w->right->color = RBTREE_BLACK;
                    w->color = RBTREE_RED;
                    rbtree_left_rotate(w);
                    w = x->parent->left;
                }

                w->color = x->parent->color;
                x->parent->color = RBTREE_BLACK;
                w->left->color = RBTREE_BLACK;
                rbtree_right_rotate(x->parent);
                x = rb_tree.root;
            }
        }
    }

    x->color = RBTREE_BLACK;
}

static void rbtree_free_node(struct rbtree_node *node) {
    kvstore_free(node->key);
    kvstore_free(node->value);
    kvstore_free(node);
}

static void rbtree_destroy_nodes(struct rbtree_node *node) {
    if (node == rb_tree.nil) {
        return;
    }

    rbtree_destroy_nodes(node->left);
    rbtree_destroy_nodes(node->right);
    rbtree_free_node(node);
}

int kvstore_rbtree_create(void) {
    if (rb_tree.nil != NULL) {
        return 0;
    }

    rb_tree.nil = kvstore_malloc(sizeof(struct rbtree_node));
    if (rb_tree.nil == NULL) {
        return -1;
    }

    rb_tree.nil->color = RBTREE_BLACK;
    rb_tree.nil->left = rb_tree.nil;
    rb_tree.nil->right = rb_tree.nil;
    rb_tree.nil->parent = rb_tree.nil;
    rb_tree.nil->key = NULL;
    rb_tree.nil->value = NULL;
    rb_tree.root = rb_tree.nil;

    return 0;
}

void kvstore_rbtree_destroy(void) {
    if (rb_tree.nil == NULL) {
        return;
    }

    rbtree_destroy_nodes(rb_tree.root);
    kvstore_free(rb_tree.nil);
    rb_tree.root = NULL;
    rb_tree.nil = NULL;
}

int kvstore_rbtree_set(char *key, char *value) {
    struct rbtree_node *parent = NULL;
    struct rbtree_node *node = NULL;
    char *key_copy = NULL;
    char *value_copy = NULL;
    int cmp = 0;

    if (key == NULL || value == NULL || rb_tree.nil == NULL) {
        return -1;
    }

    parent = rb_tree.nil;
    node = rb_tree.root;
    while (node != rb_tree.nil) {
        parent = node;
        cmp = strcmp(key, node->key);
        if (cmp == 0) {
            return 1;
        } else if (cmp < 0) {
            node = node->left;
        } else {
            node = node->right;
        }
    }

    node = kvstore_malloc(sizeof(struct rbtree_node));
    if (node == NULL) {
        return -1;
    }

    key_copy = kvstore_strdup(key);
    if (key_copy == NULL) {
        kvstore_free(node);
        return -1;
    }

    value_copy = kvstore_strdup(value);
    if (value_copy == NULL) {
        kvstore_free(key_copy);
        kvstore_free(node);
        return -1;
    }

    node->key = key_copy;
    node->value = value_copy;
    node->left = rb_tree.nil;
    node->right = rb_tree.nil;
    node->parent = parent;
    node->color = RBTREE_RED;

    if (parent == rb_tree.nil) {
        rb_tree.root = node;
    } else if (strcmp(node->key, parent->key) < 0) {
        parent->left = node;
    } else {
        parent->right = node;
    }

    rbtree_insert_fixup(node);
    return 0;
}

char *kvstore_rbtree_get(char *key) {
    struct rbtree_node *node = rbtree_search_node(key);

    if (node == NULL) {
        return NULL;
    }

    return node->value;
}

int kvstore_rbtree_del(char *key) {
    struct rbtree_node *z = NULL;
    struct rbtree_node *y = NULL;
    struct rbtree_node *x = NULL;
    int y_original_color = RBTREE_BLACK;

    if (key == NULL || rb_tree.nil == NULL) {
        return -1;
    }

    z = rbtree_search_node(key);
    if (z == NULL) {
        return 1;
    }

    /* 标准 CLRS 删除：
     *  - z 至多一个孩子：直接用那个孩子(可能是 nil)顶替 z。
     *  - z 有两个孩子：找右子树最小值 y(中序后继)顶替 z，y 搬走后由其右孩子 x 顶替 y。
     * y_original_color 记录"真正从树里消失的那个颜色"，只有它是黑时才会破坏黑高，
     * 才需要 delete_fixup(x)。x 是顶替到空位、可能背负"额外黑"的节点。 */
    y = z;
    y_original_color = y->color;

    if (z->left == rb_tree.nil) {
        x = z->right;
        rbtree_transplant(z, z->right);
    } else if (z->right == rb_tree.nil) {
        x = z->left;
        rbtree_transplant(z, z->left);
    } else {
        y = rbtree_minimum(z->right);   /* 中序后继 */
        y_original_color = y->color;
        x = y->right;

        if (y->parent == z) {
            /* y 就是 z 的右孩子：x 即便是 nil 也要把 parent 指向 y，
             * 否则 fixup 从 nil 往上回溯会走错。 */
            x->parent = y;
        } else {
            rbtree_transplant(y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }

        rbtree_transplant(z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    if (y_original_color == RBTREE_BLACK) {
        rbtree_delete_fixup(x);
    }

    rbtree_free_node(z);
    return 0;
}

int kvstore_rbtree_mod(char *key, char *value) {
    struct rbtree_node *node = NULL;
    char *value_copy = NULL;

    if (key == NULL || value == NULL || rb_tree.nil == NULL) {
        return -1;
    }

    node = rbtree_search_node(key);
    if (node == NULL) {
        return 1;
    }

    value_copy = kvstore_strdup(value);
    if (value_copy == NULL) {
        return -1;
    }

    kvstore_free(node->value);
    node->value = value_copy;
    return 0;
}

#endif
