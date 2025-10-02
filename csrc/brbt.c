#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "brbt.h"

/*
 * implementation adapted from the one
 * described in Robert Sedgewick's 2008 paper
 * on left leaning red-black trees.
 */

struct brbt_tree
{
  /* realistically void*,
   * but the alignment shouldnt really matter
   * if it ever becomes a problem, make an issue
   */
  char* data_array;
  struct brbt_bookkeeping_info* bookkeeping_array;

  /* should we invoke free() on the data array */
  bool allocated_data_array;

  /* should we invoke free() on the bookkeeping array */
  bool allocated_bookkeeping_array;

  /* size of the user data structure
   * includes trailing padding
   */
  size_t member_bytesize;

  /* byte offset into the user data structure
   * for where to find the key data
   */
  size_t member_key_offset;

  /* number of allocated nodes */
  unsigned size;

  /* maximum size of the array */
  unsigned capacity;

  /* index to the root node
   * if == BRBT_NA, then no root is specified
   */
  unsigned root;

  brbt_comparator comparator;
  brbt_deleter deleter;

  bool enforce_policy;
  struct brbt_policy policy;
};

static void
iterate_impl(struct brbt_tree* tree,
             brbt_iterator iterator,
             void* userdata,
             unsigned node);

struct brbt_tree*
brbt_create_ex(size_t member_bytesize,
               size_t capacity,
               size_t key_offset,
               struct brbt_policy* policy,
               struct brbt_bookkeeping_info* bookkeeping,
               void* data,
               brbt_deleter deleter,
               brbt_comparator comparator)
{
  assert(member_bytesize != 0);
  assert(capacity != 0);
  assert(comparator);

  struct brbt_tree* tree = malloc(sizeof *tree);

  tree->data_array = data ? data : malloc(member_bytesize * capacity);
  tree->bookkeeping_array =
    bookkeeping ? bookkeeping : malloc(sizeof *bookkeeping * capacity);

  tree->allocated_data_array = (bool)data;
  tree->allocated_bookkeeping_array = (bool)bookkeeping;

  tree->member_bytesize = member_bytesize;
  tree->member_key_offset = key_offset;

  tree->size = 0;
  tree->capacity = capacity;
  tree->root = BRBT_NA;

  tree->deleter = deleter;
  tree->comparator = comparator;
  if (policy)
    tree->enforce_policy = true, tree->policy = *policy;

  for (unsigned i = 0; i < tree->capacity; i++) {
    bookkeeping[i].free = true;
    bookkeeping[i].red = false;
    bookkeeping[i].parent = BRBT_NA;
    bookkeeping[i].left = BRBT_NA;
    bookkeeping[i].right = BRBT_NA;
  }

  return tree;
}

static inline struct brbt_bookkeeping_info*
get_bk(struct brbt_tree* tree, unsigned idx)
{
  assert(tree);
  assert(idx != BRBT_NA);

  return &tree->bookkeeping_array[idx];
}

static inline void*
get_key(struct brbt_tree* tree, char* data)
{
  assert(tree);
  assert(data);

  return &data[tree->member_key_offset];
}

void*
brbt_get(struct brbt_tree* tree, node_idx idx)
{
  assert(tree);
  assert(idx != BRBT_NA);

  return &tree->data_array[tree->member_bytesize * idx];
}

static unsigned
node_alloc(struct brbt_tree* tree)
{
  assert(tree);

  if (tree->size == tree->capacity)
    assert(false);

  for (unsigned i = 0; i < tree->capacity; i++) {
    struct brbt_bookkeeping_info* bk = get_bk(tree, i);
    if (bk->free)
      return i;
  }

  assert(false);
  return BRBT_NA;
}

static int
compare(struct brbt_tree* tree, unsigned node, void* key)
{
  assert(node != BRBT_NA);
  assert(key);

  void* key_off = get_key(tree, brbt_get(tree, node));
  return tree->comparator(key, key_off);
}

static unsigned
rotate(struct brbt_tree* tree, unsigned h, bool left)
{
  assert(tree);
  assert(h != BRBT_NA);

  unsigned parent = get_bk(tree, h)->parent;
  unsigned x;

  struct brbt_bookkeeping_info* p =
    parent == BRBT_NA ? NULL : get_bk(tree, parent);

  if (left) {
    x = get_bk(tree, h)->right;
    get_bk(tree, h)->right = get_bk(tree, x)->left;
    get_bk(tree, x)->left = h;
  } else {
    x = get_bk(tree, h)->left;
    get_bk(tree, h)->left = get_bk(tree, x)->right;
    get_bk(tree, x)->right = h;
  }

  get_bk(tree, h)->parent = x;
  get_bk(tree, x)->parent = parent;

  if (p) {
    if (p->left == h)
      p->left = x;
    else
      p->right = x;
  } else
    tree->root = x;

  assert(x != BRBT_NA);

  get_bk(tree, x)->red = get_bk(tree, h)->red;
  get_bk(tree, h)->red = true;

  return x;
}

static void
color_flip(struct brbt_tree* tree, unsigned node)
{
  assert(tree);
  assert(node != BRBT_NA);

  struct brbt_bookkeeping_info* bk = get_bk(tree, node);
  assert(bk->left != BRBT_NA);
  assert(bk->right != BRBT_NA);

  bk->red ^= true;
  get_bk(tree, bk->left)->red ^= true;
  get_bk(tree, bk->right)->red ^= true;
}

static inline bool
is_red(struct brbt_tree* tree, unsigned node)
{
  assert(tree);

  /* nil nodes considered black */
  if (node == BRBT_NA)
    return false;

  return get_bk(tree, node)->red;
}

static unsigned
new_node(struct brbt_tree* tree, void* data_in)
{
  assert(tree);
  assert(data_in);

  unsigned node = node_alloc(tree);

  struct brbt_bookkeeping_info* bk = get_bk(tree, node);
  bk->free = false;
  bk->red = true;
  bk->left = BRBT_NA;
  bk->right = BRBT_NA;
  bk->parent = BRBT_NA;

  void* data = brbt_get(tree, node);
  memcpy(data, data_in, tree->member_bytesize);

  return node;
}

static unsigned
fixup(struct brbt_tree* tree, unsigned node)
{
  assert(tree);
  assert(node != BRBT_NA);

  struct brbt_bookkeeping_info* bk = get_bk(tree, node);

  if (is_red(tree, bk->right) && !is_red(tree, bk->left))
    node = rotate(tree, node, true), bk = get_bk(tree, node);

  if (is_red(tree, bk->left) && is_red(tree, get_bk(tree, bk->left)->left))
    node = rotate(tree, node, false), bk = get_bk(tree, node);

  if (is_red(tree, bk->left) && is_red(tree, bk->right))
    color_flip(tree, node);

  return node;
}

static unsigned
insert_impl(struct brbt_tree* tree,
            unsigned node,
            unsigned parent,
            void* data,
            bool replace)
{
  if (node == BRBT_NA) {
    node = new_node(tree, data);
    if (parent != BRBT_NA)
      get_bk(tree, node)->parent = parent;
    return node;
  }

  struct brbt_bookkeeping_info* bk = get_bk(tree, node);

  int cmp = compare(tree, node, get_key(tree, data));

  if (cmp == 0)
    memcpy(brbt_get(tree, node), data, tree->member_bytesize);
  else if (cmp < 0)
    bk->left = insert_impl(tree, bk->left, node, data, replace);
  else
    bk->right = insert_impl(tree, bk->right, node, data, replace);

  node = fixup(tree, node);

  return node;
}

node_idx
brbt_insert(struct brbt_tree* tree, void* node_in, bool replace)
{
  assert(tree);
  assert(node_in);

  if (tree->root == BRBT_NA) {
    tree->root = 0;
    tree->bookkeeping_array[0].left = BRBT_NA;
    tree->bookkeeping_array[0].right = BRBT_NA;
    tree->bookkeeping_array[0].parent = BRBT_NA;
    tree->bookkeeping_array[0].red = 0;
    tree->bookkeeping_array[0].free = false;

    memcpy(brbt_get(tree, 0), node_in, tree->member_bytesize);

    return 0;
  }

  return insert_impl(tree, tree->root, BRBT_NA, node_in, replace);
}

node_idx
brbt_find(struct brbt_tree* tree, void* key)
{
  assert(tree);
  assert(key);

  unsigned current_node = tree->root;

  while (current_node != BRBT_NA) {
    int comp = compare(tree, current_node, key);

    if (comp > 0) {
      current_node = get_bk(tree, current_node)->right;
    } else if (comp < 0) {
      current_node = get_bk(tree, current_node)->left;
    } else
      return current_node;
  }

  return BRBT_NA;
}

static unsigned
move_red_left(struct brbt_tree* tree, unsigned h)
{
  assert(tree);
  assert(h != BRBT_NA);

  struct brbt_bookkeeping_info *bk = get_bk(tree, h),
                               *bkr = get_bk(tree, bk->right);

  color_flip(tree, h);

  if (is_red(tree, bkr->left)) {
    bk->right = rotate(tree, bk->right, false);
    h = rotate(tree, h, true);
    color_flip(tree, h);
  }

  return h;
}

static unsigned
move_red_right(struct brbt_tree* tree, unsigned h)
{
  assert(tree);
  assert(h != BRBT_NA);

  struct brbt_bookkeeping_info *bk = get_bk(tree, h),
                               *bkl = get_bk(tree, bk->left);

  color_flip(tree, h);

  if (is_red(tree, bkl->left)) {
    h = rotate(tree, h, false);
    color_flip(tree, h);
  }

  return h;
}

node_idx
brbt_minimum(struct brbt_tree* tree, unsigned node)
{
  assert(tree);
  assert(node != BRBT_NA);

  struct brbt_bookkeeping_info* bk = get_bk(tree, node);
  while (bk->left != BRBT_NA) {
    node = bk->left;
    bk = get_bk(tree, node);
  }

  return node;
}

static node_idx
delete_min_impl(struct brbt_tree* tree, node_idx node, bool call_deleter)
{
  struct brbt_bookkeeping_info* bk = get_bk(tree, node);

  if (bk->left == BRBT_NA)
    return BRBT_NA;

  if (!is_red(tree, bk->left) && !is_red(tree, get_bk(tree, bk->left)->left))
    node = move_red_left(tree, node), bk = get_bk(tree, node);

  node_idx ret = delete_min_impl(tree, bk->left, call_deleter);

  if (ret == BRBT_NA && call_deleter)
    tree->deleter(tree, bk->left);

  bk->left = ret;

  return fixup(tree, node);
}

void
brbt_delete_min(struct brbt_tree* tree, node_idx node)
{
  delete_min_impl(tree, node, true);
}

static unsigned
delete_impl(struct brbt_tree* tree, unsigned node, void* key)
{
  struct brbt_bookkeeping_info* bk = get_bk(tree, node);

  if (compare(tree, node, key) < 0) {
    struct brbt_bookkeeping_info* bkl = get_bk(tree, bk->left);

    if (!is_red(tree, bk->left) && !is_red(tree, bkl->left))
      node = move_red_left(tree, node), bk = get_bk(tree, node);

    node_idx tmp = delete_impl(tree, bk->left, key);
    if (tmp == BRBT_NA && tree->deleter)
      tree->deleter(tree, node);

    bk->left = tmp;
  } else {
    if (is_red(tree, bk->left))
      node = rotate(tree, node, false), bk = get_bk(tree, node);

    if (compare(tree, node, key) == 0 && bk->right == BRBT_NA)
      return BRBT_NA;

    if (!is_red(tree, bk->right) &&
        !is_red(tree, get_bk(tree, bk->right)->left))
      node = move_red_right(tree, node), bk = get_bk(tree, node);

    if (compare(tree, node, key) == 0) {
      /* make sure the nodes data is dealt with... */
      if (tree->deleter)
        tree->deleter(tree, node);

      node_idx min = brbt_minimum(tree, bk->right);

      struct brbt_bookkeeping_info* pbk =
        bk->parent != BRBT_NA ? get_bk(tree, bk->parent) : NULL;
      struct brbt_bookkeeping_info* mbk = get_bk(tree, min);

      if (pbk)
        (pbk->left == node) ? (pbk->left = min) : (pbk->right = min);
      else
        tree->root = min;

      mbk->parent = bk->parent;
      mbk->left = bk->left;

      if (bk->right != BRBT_NA)
        get_bk(tree, bk->right)->parent = min;
      if (mbk->left != BRBT_NA)
        get_bk(tree, mbk->left)->parent = min;

      mbk->right = delete_min_impl(tree, bk->right, false);
    } else
      bk->right = delete_impl(tree, bk->right, key);
  }

  return fixup(tree, node);
}

/* deletes a node with a given key */
void
brbt_delete(struct brbt_tree* tree, void* key)
{
  delete_impl(tree, tree->root, key);
}

static void
iterate_impl(struct brbt_tree* tree,
             brbt_iterator iterator,
             void* userdata,
             unsigned node)
{
  if (node == BRBT_NA)
    return;

  struct brbt_bookkeeping_info* bk = get_bk(tree, node);
  iterate_impl(tree, iterator, userdata, bk->left);
  iterator(tree, userdata, node);
  iterate_impl(tree, iterator, userdata, bk->right);
}

void
brbt_iterate(struct brbt_tree* tree, brbt_iterator iterate, void* userdata)
{
  iterate_impl(tree, iterate, userdata, tree->root);
}

node_idx
brbt_root(struct brbt_tree* tree)
{
  return tree->root;
}
