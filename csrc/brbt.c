#include <assert.h>
#include <stddef.h>
#include <stdio.h>
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

  /* first free node in the list */
  node_idx first_free;

  brbt_comparator comparator;
  brbt_deleter deleter;

  bool enforce_policy;
  struct brbt_policy policy;
};

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
    bookkeeping[i].red = false;
    bookkeeping[i].left = BRBT_NA;
    bookkeeping[i].right = BRBT_NA;

    if (i < tree->capacity - 1)
      bookkeeping[i].next_free = i + 1;
    else
      bookkeeping[i].next_free = BRBT_NA;
  }

  tree->first_free = 0;

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

#define left(x) get_bk(tree, x)->left
#define right(x) get_bk(tree, x)->right
#define col(x) get_bk(tree, x)->red

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

  if (tree->size >= tree->capacity)
    assert(false);

  tree->size++;

  node_idx h = tree->first_free;
  tree->first_free = get_bk(tree, h)->next_free;

  printf("allocated %i\n", h);

  return h;
}

static inline int
compare(struct brbt_tree* tree, node_idx node, void* key)
{
  assert(node != BRBT_NA);
  assert(key);

  void* key_off = get_key(tree, brbt_get(tree, node));
  return tree->comparator(key, key_off);
}

#define compare(node, key) compare(tree, node, key)

static node_idx
rotate_left(struct brbt_tree* tree, node_idx h)
{
  assert(tree);
  assert(h != BRBT_NA);

  node_idx x = right(h);
  right(h) = left(x);
  left(x) = h;
  col(x) = col(h);
  col(h) = true;
  return x;
}

static node_idx
rotate_right(struct brbt_tree* tree, node_idx h)
{
  assert(tree);
  assert(h != BRBT_NA);

  node_idx x = left(h);
  left(h) = right(x);
  right(x) = h;
  col(x) = col(h);
  col(h) = true;
  return x;
}

#define rotl(x) rotate_left(tree, x)
#define rotr(x) rotate_right(tree, x)

static void
color_flip(struct brbt_tree* tree, node_idx node)
{
  assert(tree);
  assert(node != BRBT_NA);

  col(node) = !col(node);
  col(left(node)) = !col(left(node));
  col(right(node)) = !col(right(node));
}

#define color_flip(x) color_flip(tree, x)

static inline bool
is_red(struct brbt_tree* tree, node_idx node)
{
  assert(tree);

  /* nil nodes considered black */
  if (node == BRBT_NA)
    return false;

  return col(node);
}

#define is_red(x) is_red(tree, x)

static node_idx
new_node(struct brbt_tree* tree, void* data_in)
{
  assert(tree);
  assert(data_in);

  node_idx node = node_alloc(tree);
  col(node) = true;
  left(node) = BRBT_NA;
  right(node) = BRBT_NA;

  void* data = brbt_get(tree, node);
  memcpy(data, data_in, tree->member_bytesize);

  return node;
}

__attribute__((hot, flatten)) static node_idx
insert_impl(struct brbt_tree* tree, node_idx h, void* data, bool replace)
{
  if (h == BRBT_NA)
    return new_node(tree, data);

  int cmp = compare(h, get_key(tree, data));

  if (cmp == 0) {
    if (replace) {
      tree->deleter(tree, h);
      memcpy(brbt_get(tree, h), data, tree->member_bytesize);
    }
  } else if (cmp < 0)
    left(h) = insert_impl(tree, left(h), data, replace);
  else
    right(h) = insert_impl(tree, right(h), data, replace);

  if (is_red(right(h)) && !is_red(left(h)))
    h = rotl(h);
  if (is_red(left(h)) && is_red(left(left(h))))
    h = rotr(h);
  if (is_red(left(h)) && is_red(right(h)))
    color_flip(h);

  return h;
}

node_idx
brbt_insert(struct brbt_tree* tree, void* node_in, bool replace)
{
  assert(tree);
  assert(node_in);

  return tree->root = insert_impl(tree, tree->root, node_in, replace);
}

node_idx
brbt_find(struct brbt_tree* tree, void* key)
{
  assert(tree);
  assert(key);

  unsigned current_node = tree->root;

  while (current_node != BRBT_NA) {
    int comp = compare(current_node, key);

    if (comp > 0)
      current_node = right(current_node);
    else if (comp < 0)
      current_node = left(current_node);
    else
      return current_node;
  }

  return BRBT_NA;
}

node_idx
brbt_minimum(struct brbt_tree* tree, unsigned node)
{
  assert(tree);
  assert(node != BRBT_NA);

  while (left(node) != BRBT_NA)
    node = left(node);

  return node;
}

static node_idx
on_delete(struct brbt_tree* tree, node_idx to_del, bool call_deleter)
{
  assert(to_del != tree->first_free);
  tree->size--;

  if (tree->deleter && call_deleter)
    tree->deleter(tree, to_del);

  if (tree->first_free == BRBT_NA) {
    /* no other free nodes */
    tree->first_free = to_del;
    get_bk(tree, to_del)->next_free = BRBT_NA;
  } else if (to_del < tree->first_free) {
    /* this node is before the first free node */
    get_bk(tree, to_del)->next_free = tree->first_free;
    tree->first_free = to_del;
  } else {
    /* this node is after the first free node */
    node_idx previous = tree->first_free;
    node_idx follower = get_bk(tree, tree->first_free)->next_free;

    while (to_del > follower) {
      previous = follower;
      follower = get_bk(tree, follower)->next_free;
    }

    get_bk(tree, previous)->next_free = to_del;
    get_bk(tree, to_del)->next_free = follower;
  }

  return to_del;
}

static unsigned
move_red_left(struct brbt_tree* tree, unsigned h)
{
  assert(tree);
  assert(h != BRBT_NA);

  color_flip(h);

  if (right(h) != BRBT_NA && is_red(left(right(h)))) {
    right(h) = rotr(right(h));
    h = rotl(h);
    color_flip(h);
  }

  return h;
}

static unsigned
move_red_right(struct brbt_tree* tree, unsigned h)
{
  assert(tree);
  assert(h != BRBT_NA);

  color_flip(h);

  if (left(h) != BRBT_NA && is_red(left(left(h)))) {
    h = rotr(h);
    color_flip(h);
  }

  return h;
}

static node_idx
delete_min_impl(struct brbt_tree* tree, node_idx h, bool call_deleter)
{
  assert(tree);
  assert(h != BRBT_NA);

  if (left(h) == BRBT_NA)
    return BRBT_NA;

  if (!is_red(left(h)) && !is_red(left(left(h))))
    h = move_red_left(tree, h);

  left(h) = delete_min_impl(tree, left(h), call_deleter);

  if (is_red(right(h)))
    h = rotl(h);
  if (is_red(left(h)) && is_red(left(left(h))))
    h = rotr(h);
  if (is_red(left(h)) && is_red(right(h)))
    color_flip(h);

  return h;
}

void
brbt_delete_min(struct brbt_tree* tree, node_idx node)
{
  if (node == BRBT_NA)
    node = tree->root;
  delete_min_impl(tree, node, true);
}

static node_idx
delete_impl(struct brbt_tree* tree, unsigned h, void* key)
{
#define del(node, key) delete_impl(tree, node, key)
  if (h == BRBT_NA)
    return BRBT_NA;

  if (compare(h, key) < 0) {
    if (left(h) != BRBT_NA && !is_red(left(h)) && !is_red(left(left(h)))) {
      h = move_red_left(tree, h);
      left(h) = del(left(h), key);
    }
  } else {
    if (is_red(left(h)))
      h = rotr(h);

    if (compare(h, key) == 0 && (right(h) == BRBT_NA))
      return BRBT_NA;

    if (right(h) != BRBT_NA) {
      if (!is_red(right(h)) && !is_red(left(right(h))))
        h = move_red_right(tree, h);

      if (compare(h, key) == 0) {
        node_idx min = brbt_minimum(tree, right(h));
        right(h) = delete_min_impl(tree, right(h), false);
        left(min) = left(h);
        right(min) = right(h);
        h = min;
      } else
        right(h) = del(right(h), key);
    }
  }

  if (is_red(right(h)))
    h = rotl(h);
  if (is_red(left(h)) && is_red(left(left(h))))
    h = rotr(h);
  if (is_red(left(h)) && is_red(right(h)))
    color_flip(h);

  return h;
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

  iterate_impl(tree, iterator, userdata, left(node));
  iterator(tree, userdata, node);
  iterate_impl(tree, iterator, userdata, right(node));
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
