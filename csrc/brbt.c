#include "brbt.h"

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

/*
 * implementation adapted from the one
 * described in Robert Sedgewick's 2008 paper
 * on left leaning red-black trees.
 */

#define left(x) get_bk(tree, x)->left
#define right(x) get_bk(tree, x)->right
#define col(x) get_bk(tree, x)->red
#define nextfree(x) get_bk(tree, x)->next_free
#define assert(x) ((x) ? (void)(0) : tree->_policy->abort(tree, __LINE__))
#define keyoff tree->_type->keyoff
#define membs tree->_type->membs

static inline struct brbt_bookkeeping_info*
get_bk(struct brbt* tree, unsigned idx)
{
  assert(tree);
  assert(idx != BRBT_NIL);

  return &tree->bk[idx];
}

static inline void*
get_key(struct brbt* tree, char* data)
{
  assert(tree);
  assert(data);

  return &data[keyoff];
}

void*
brbt_get(struct brbt* tree, brbt_node idx)
{
  assert(tree);
  assert(idx != BRBT_NIL);

  return &tree->ptr[membs * idx];
}

struct brbt
brbt_create(struct brbt_type const* type,
            struct brbt_policy const* policy,
            void* userdata)
{
  struct brbt tree;

  tree.ptr = NULL;
  tree.bk = NULL;
  tree.size = 0;
  tree.capacity = 0;
  tree.first_free = 0;
  tree.root = BRBT_NIL;

  tree._policy = policy;
  tree._type = type;
  tree.userdata = userdata;

  return tree;
}

unsigned
brbt_size(struct brbt* tree)
{
  return tree->size;
}

unsigned
brbt_capacity(struct brbt* tree)
{
  return tree->capacity;
}

static brbt_node
node_free(struct brbt* tree, brbt_node h)
{
  tree->size--;

  if (tree->_type->deleter)
    tree->_type->deleter(tree, h);

  if (tree->_policy->remove_hook)
    tree->_policy->remove_hook(tree, h);

  if (tree->first_free == BRBT_NIL) {
    /* no other free nodes */
    tree->first_free = h;
    nextfree(h) = BRBT_NIL;
  } else {
    /* this node is before the first free node */
    nextfree(h) = tree->first_free;
    tree->first_free = h;
  }

  return h;
}

/* can return BRBT_NIL if allocation fail occurs */
static brbt_node
node_alloc(struct brbt* tree)
{
  assert(tree);

  if (tree->size >= tree->capacity) {
    /* try to reallocate */
    if (tree->_policy->resize) {
      unsigned const old_cap = tree->capacity;
      struct brbt_allocator_out out = tree->_policy->resize(tree, BRBT_GROW);
      tree->ptr = out.data_array;
      tree->bk = out.bk_array;
      tree->capacity = out.size;
      unsigned const new_cap = tree->capacity;

      /* TODO: implement the -1u marker/init technique i use elsewhere
       * as to amortize the initialization costs of
       * allocating many nodes */
      for (unsigned i = old_cap; i < new_cap; i++) {
        tree->bk[i].left = BRBT_NIL;
        tree->bk[i].right = BRBT_NIL;

        if (i < tree->capacity - 1)
          tree->bk[i].next_free = i + 1;
        else
          tree->bk[i].next_free = BRBT_NIL;
      }

    } else {
      /* if we cant reallocate to gain a larger capacity,
       * we need to maybe remove a value.
       * if theres no free function, just assert false
       * (in the future, we should just fail insertion functions)
       */
      assert(tree->_policy->select);
      node_free(tree, tree->_policy->select(tree));
    }
  }

  tree->size++;

  brbt_node h = tree->first_free;
  tree->first_free = nextfree(h);

  return h;
}

static void
clear_iterator(struct brbt* tree, [[maybe_unused]] void* user, brbt_node node)
{
  node_free(tree, node);
}

void
brbt_clear(struct brbt* tree)
{
  if (tree->size == 0)
    return;

  brbt_iterate(tree, clear_iterator, NULL);
  tree->size = 0;
  tree->root = BRBT_NIL;

  // /* clear all the nodes before the first free node */
  // for (brbt_node i = 0; i < tree->first_free; i++)
  //   node_free(tree, i);

  // /* for each free node, clear the nodes after it and
  //  * before the next free node
  //  */

  // brbt_node current = tree->first_free;
  // while (current != BRBT_NIL) {
  //   brbt_node until = nextfree(current);
  //   if (until == BRBT_NIL)
  //     until = tree->capacity;

  //   for (brbt_node i = current + 1; i < until; i++)
  //     node_free(tree, i);
  // }
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

  tree->_policy->free(tree);
}

static inline int
compare(struct brbt* tree, brbt_node node, void const* key)
{
  assert(node != BRBT_NIL);
  assert(key);

  void* key_off = get_key(tree, brbt_get(tree, node));
  return tree->_type->cmp(key, key_off);
}

#define compare(node, key) compare(tree, node, key)

static inline _Bool
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
  if (node == BRBT_NIL)
    return BRBT_NIL;
  col(node) = true;
  left(node) = BRBT_NIL;
  right(node) = BRBT_NIL;

  void* data = brbt_get(tree, node);
  __builtin_memcpy(data, data_in, membs);

  if (tree->_policy->insert_hook)
    tree->_policy->insert_hook(tree, node);

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
insert_impl(struct brbt* tree,
            brbt_node node,
            void* data,
            _Bool replace,
            brbt_node* insert_out)
{
  if (node == BRBT_NIL) {
    *insert_out = new_node(tree, data);
    return *insert_out;
  }

  int cmp = compare(node, get_key(tree, data));

  if (cmp == 0) {
    if (replace) {
      tree->_type->deleter(tree, node);
      __builtin_memcpy(brbt_get(tree, node), data, membs);
    }
  } else if (cmp < 0)
    left(node) = insert_impl(tree, left(node), data, replace, insert_out);
  else
    right(node) = insert_impl(tree, right(node), data, replace, insert_out);

  return fixup(tree, node);
}

brbt_node
brbt_insert(struct brbt* tree, void* node_in, _Bool replace)
{
  assert(tree);
  assert(node_in);

  brbt_node out;
  tree->root = insert_impl(tree, tree->root, node_in, replace, &out);
  return out;
}

brbt_node
brbt_find(struct brbt* tree, void const* key)
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
delete_min_impl(struct brbt* tree, brbt_node h, _Bool free_node)
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

void
brbt_iterate(struct brbt* tree, brbt_iterator iterate, void* userdata)
{
  /* up to 2^32 node depth */
  struct state
  {
    brbt_node node;
    enum
    {
      LHS,
      SELF,
    } state;
  } stack[32];
  unsigned si = 0;

  if (tree->size == 0)
    return;

  stack[si].state = LHS;
  stack[si++].node = tree->root;

  while (si > 0) {
    struct state* state = &stack[si - 1];
    if (state->node == BRBT_NIL) {
      si--;
      continue;
    }

    if (state->state == LHS) {
      brbt_node lhs = left(state->node);
      state->state = SELF;
      stack[si].state = LHS;
      stack[si++].node = lhs;
    } else {
      iterate(tree, userdata, state->node);
      state->node = right(state->node);
      state->state = LHS;
    }
  }
}

brbt_node
brbt_root(struct brbt* tree)
{
  return tree->root;
}
