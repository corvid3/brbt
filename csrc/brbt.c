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

struct brbt
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
   * if == BRBT_NIL, then no root is specified
   */
  unsigned root;

  /* first free node in the list */
  brbt_node first_free;

  brbt_comparator comparator;
  brbt_deleter deleter;

  bool enforce_policy;
  struct brbt_policy policy;
};

#define left(x) get_bk(tree, x)->left
#define right(x) get_bk(tree, x)->right
#define col(x) get_bk(tree, x)->red
#define nextfree(x) get_bk(tree, x)->next_free

static inline struct brbt_bookkeeping_info*
get_bk(struct brbt* tree, unsigned idx)
{
  assert(tree);
  assert(idx != BRBT_NIL);

  return &tree->bookkeeping_array[idx];
}

static inline void*
get_key(struct brbt* tree, char* data)
{
  assert(tree);
  assert(data);

  return &data[tree->member_key_offset];
}

void*
brbt_get(struct brbt* tree, brbt_node idx)
{
  assert(tree);
  assert(idx != BRBT_NIL);

  return &tree->data_array[tree->member_bytesize * idx];
}

struct brbt*
brbt_create(size_t member_bytesize,
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

  struct brbt* tree = malloc(sizeof *tree);

  tree->data_array = data ? data : malloc(member_bytesize * capacity);
  tree->bookkeeping_array =
    bookkeeping ? bookkeeping : malloc(sizeof *bookkeeping * capacity);
  assert(tree->data_array);
  assert(tree->bookkeeping_array);

  tree->allocated_data_array = !(bool)data;
  tree->allocated_bookkeeping_array = !(bool)bookkeeping;

  tree->member_bytesize = member_bytesize;
  tree->member_key_offset = key_offset;

  tree->size = 0;
  tree->capacity = capacity;
  tree->root = BRBT_NIL;

  tree->deleter = deleter;
  tree->comparator = comparator;

  if (policy) {
    assert(policy->begin);
    assert(policy->decide);
    tree->enforce_policy = true, tree->policy = *policy;
  } else
    tree->enforce_policy = false;

  for (unsigned i = 0; i < tree->capacity; i++) {
    bookkeeping[i].red = false;
    bookkeeping[i].left = BRBT_NIL;
    bookkeeping[i].right = BRBT_NIL;

    if (i < tree->capacity - 1)
      bookkeeping[i].next_free = i + 1;
    else
      bookkeeping[i].next_free = BRBT_NIL;
  }

  tree->first_free = 0;

  return tree;
}

static brbt_node
node_free(struct brbt* tree, brbt_node h)
{
  tree->size--;

  if (tree->deleter)
    tree->deleter(tree, h);

  if (tree->enforce_policy)
    tree->policy.remove_hook(tree, tree->policy.policy_data, h);

  if (tree->first_free == BRBT_NIL) {
    /* no other free nodes */
    tree->first_free = h;
    nextfree(h) = BRBT_NIL;
  } else if (h < tree->first_free) {
    /* this node is before the first free node */
    nextfree(h) = tree->first_free;
    tree->first_free = h;
  } else if (h > tree->first_free) {
    /* this node is after the first free node */
    brbt_node i = tree->first_free;

    while (h > nextfree(i))
      i = nextfree(i);

    nextfree(h) = nextfree(i);
    nextfree(i) = h;
  } else
    exit(1);

  return h;
}

static brbt_node
node_alloc(struct brbt* tree)
{
  assert(tree);

  if (tree->size >= tree->capacity) {
    if (tree->enforce_policy) {
      tree->policy.begin(tree->policy.policy_data);
      if (tree->policy.run)
        for (brbt_node i = 0; i < tree->capacity; i++)
          tree->policy.run(tree->policy.policy_data, i);
      brbt_node replace = tree->policy.decide(tree->policy.policy_data);
      node_free(tree, replace);
    } else
      assert(false);
  }

  tree->size++;

  brbt_node h = tree->first_free;
  tree->first_free = nextfree(h);

  return h;
}

void
brbt_destroy(struct brbt* tree)
{
  volatile brbt_node begin = 0;
  brbt_node follower = tree->first_free;

  while (begin != BRBT_NIL) {
    brbt_node end = follower == BRBT_NIL ? tree->capacity : follower;

    for (brbt_node i = begin; i < end; i++)
      node_free(tree, i);

    begin = follower;
    follower = follower == BRBT_NIL ? 0 : nextfree(follower);
  }

  if (tree->allocated_bookkeeping_array && tree->data_array)
    free(tree->bookkeeping_array);
  if (tree->allocated_data_array && tree->data_array)
    free(tree->data_array);
}

static inline int
compare(struct brbt* tree, brbt_node node, void* key)
{
  assert(node != BRBT_NIL);
  assert(key);

  void* key_off = get_key(tree, brbt_get(tree, node));
  return tree->comparator(key, key_off);
}

#define compare(node, key) compare(tree, node, key)

static inline bool
is_red(struct brbt* tree, brbt_node node)
{
  assert(tree);

  /* nil nodes considered black */
  if (node == BRBT_NIL)
    return false;

  return col(node);
}

#define is_red(x) is_red(tree, x)

static brbt_node
rotate_left(struct brbt* tree, brbt_node h)
{
  assert(tree);
  assert(h != BRBT_NIL);
  assert(is_red(right(h)));

  brbt_node x = right(h);
  right(h) = left(x);
  left(x) = h;
  col(x) = col(h);
  col(h) = true;
  return x;
}

static brbt_node
rotate_right(struct brbt* tree, brbt_node h)
{
  assert(tree);
  assert(h != BRBT_NIL);
  assert(is_red(left(h)));

  brbt_node x = left(h);
  left(h) = right(x);
  right(x) = h;
  col(x) = col(h);
  col(h) = true;
  return x;
}

#define rotl(x) rotate_left(tree, x)
#define rotr(x) rotate_right(tree, x)

static void
color_flip(struct brbt* tree, brbt_node node)
{
  assert(tree);
  assert(node != BRBT_NIL);

  col(node) = !col(node);
  if (left(node) != BRBT_NIL)
    col(left(node)) = !col(left(node));
  if (right(node) != BRBT_NIL)
    col(right(node)) = !col(right(node));
}

#define color_flip(x) color_flip(tree, x)

static brbt_node
new_node(struct brbt* tree, void* data_in)
{
  assert(tree);
  assert(data_in);

  brbt_node node = node_alloc(tree);
  col(node) = true;
  left(node) = BRBT_NIL;
  right(node) = BRBT_NIL;

  void* data = brbt_get(tree, node);
  memcpy(data, data_in, tree->member_bytesize);

  if (tree->enforce_policy)
    tree->policy.insert_hook(tree, tree->policy.policy_data, node);

  return node;
}

static brbt_node
fixup(struct brbt* tree, brbt_node h)
{
  assert(tree);
  assert(h != BRBT_NIL);

  if (is_red(right(h)) && !is_red(left(h)))
    h = rotl(h);
  if (is_red(left(h)) && is_red(left(left(h))))
    h = rotr(h);
  if (is_red(left(h)) && is_red(right(h)))
    color_flip(h);

  return h;
}

__attribute__((hot, flatten)) static brbt_node
insert_impl(struct brbt* tree, brbt_node node, void* data, bool replace)
{
  if (node == BRBT_NIL)
    return new_node(tree, data);

  int cmp = compare(node, get_key(tree, data));

  if (cmp == 0) {
    if (replace) {
      tree->deleter(tree, node);
      memcpy(brbt_get(tree, node), data, tree->member_bytesize);
    }
  } else if (cmp < 0)
    left(node) = insert_impl(tree, left(node), data, replace);
  else
    right(node) = insert_impl(tree, right(node), data, replace);

  return fixup(tree, node);
}

brbt_node
brbt_insert(struct brbt* tree, void* node_in, bool replace)
{
  assert(tree);
  assert(node_in);

  return tree->root = insert_impl(tree, tree->root, node_in, replace);
}

brbt_node
brbt_find(struct brbt* tree, void* key)
{
  assert(tree);
  assert(key);

  unsigned current_node = tree->root;

  while (current_node != BRBT_NIL) {
    int comp = compare(current_node, key);

    if (comp > 0)
      current_node = right(current_node);
    else if (comp < 0)
      current_node = left(current_node);
    else
      return current_node;
  }

  return BRBT_NIL;
}

brbt_node
brbt_minimum(struct brbt* tree, unsigned node)
{
  assert(tree);
  assert(node != BRBT_NIL);

  while (left(node) != BRBT_NIL)
    node = left(node);

  return node;
}

static unsigned
move_red_left(struct brbt* tree, unsigned h)
{
  assert(tree);
  assert(h != BRBT_NIL);

  color_flip(h);

  if (right(h) != BRBT_NIL && is_red(left(right(h)))) {
    right(h) = rotr(right(h));
    h = rotl(h);
    color_flip(h);
  }

  return h;
}

static unsigned
move_red_right(struct brbt* tree, unsigned h)
{
  assert(tree);
  assert(h != BRBT_NIL);

  color_flip(h);

  if (left(h) != BRBT_NIL && is_red(left(left(h)))) {
    h = rotr(h);
    color_flip(h);
  }

  return h;
}

static brbt_node
delete_min_impl(struct brbt* tree, brbt_node h, bool free_node)
{
  if (!tree)
    return 0;
  if (h == BRBT_NIL)
    return 0;

  assert(tree);
  assert(h != BRBT_NIL);

  if (left(h) == BRBT_NIL) {
    if (free_node)
      node_free(tree, h);
    return BRBT_NIL;
  }
  if (!is_red(left(h)) && !is_red(left(left(h))))
    h = move_red_left(tree, h);

  left(h) = delete_min_impl(tree, left(h), free_node);

  return fixup(tree, h);
}

void
brbt_delete_min(struct brbt* tree, brbt_node node)
{
  if (node == BRBT_NIL)
    node = tree->root;

  delete_min_impl(tree, node, true);
}

static brbt_node
delete_impl(struct brbt* tree, unsigned h, void* key)
{
  if (!tree)
    return 0;
  if (h == BRBT_NIL)
    return 0;
  if (!key)
    return 0;

  assert(tree);
  assert(h != BRBT_NIL);
  assert(key);

#define del(node, key) delete_impl(tree, node, key)
  if (compare(h, key) < 0) {
    if (!is_red(left(h)) && !is_red(left(left(h))))
      h = move_red_left(tree, h);

    left(h) = del(left(h), key);
  } else {
    if (is_red(left(h)))
      h = rotr(h);

    if (compare(h, key) == 0 && (right(h) == BRBT_NIL))
      return node_free(tree, h), BRBT_NIL;

    if (!is_red(right(h)) && !is_red(left(right(h))))
      h = move_red_right(tree, h);

    if (compare(h, key) == 0) {
      brbt_node min = brbt_minimum(tree, right(h));
      right(h) = delete_min_impl(tree, right(h), false);
      left(min) = left(h);
      right(min) = right(h);
      node_free(tree, h);
      h = min;
    } else
      right(h) = del(right(h), key);
  }

  return fixup(tree, h);
}

/* deletes a node with a given key */
void
brbt_delete(struct brbt* tree, void* key)
{
  tree->root = delete_impl(tree, tree->root, key);
}

static void
iterate_impl(struct brbt* tree,
             brbt_iterator iterator,
             void* userdata,
             unsigned node)
{
  if (node == BRBT_NIL)
    return;

  iterate_impl(tree, iterator, userdata, left(node));
  iterator(tree, userdata, node);
  iterate_impl(tree, iterator, userdata, right(node));
}

void
brbt_iterate(struct brbt* tree, brbt_iterator iterate, void* userdata)
{
  iterate_impl(tree, iterate, userdata, tree->root);
}

brbt_node
brbt_root(struct brbt* tree)
{
  return tree->root;
}
