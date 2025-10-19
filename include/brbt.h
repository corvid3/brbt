#pragma once

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>

#define BRBT_NIL ((unsigned)-1)

struct brbt;
typedef unsigned brbt_node;

enum brbt_node_flags : unsigned char
{
  BRBT_NODE_FREE = 0b01,
  BRBT_NODE_RED = 0b10,
};

struct brbt_bookkeeping_info
{
  brbt_node left, right;

  union
  {
    brbt_node next_free;
    bool red;
  };
};

/*
 * NOTE: do not pass any data structure that is aligned to
 *  greater than max_align_t to brbt!
 */

typedef void (*brbt_iterator)(struct brbt*, void* userdata, brbt_node);

typedef int (*brbt_comparator)(void const* key_lhs, void const* key_rhs);
typedef void (*brbt_deleter)(struct brbt*, brbt_node);

/* function ran when the internal tree becomes full, and
 * the array is defined by the user, therefore the array may not be
 * reallocated.
 * the function must return a valid node index to which node should be freed
 */
typedef brbt_node (*brbt_policy_select)(struct brbt*, void* userdata);

/* hook to be ran whenever a node is inserted into the tree
 * allows one to maintain a bookkeeping list at an amortized cost
 */
typedef void (*brbt_policy_insert_hook)(struct brbt*,
                                        void* userdata,
                                        brbt_node);

/* hook to be ran whenever a node is removed from the tree
 * allows one to maintain a bookkeeping list at an amortized cost
 */
typedef void (*brbt_policy_remove_hook)(struct brbt*,
                                        void* userdata,
                                        brbt_node);

typedef void (*brbt_free)(struct brbt*,
                          void* userdata,
                          void* array,
                          struct brbt_bookkeeping_info* bk);

typedef struct brbt_allocator_out (*brbt_page_allocator)(struct brbt*,
                                                         void* userdata);

typedef struct brbt_allocator_out (*brbt_reallocator)(
  struct brbt*,
  void* userdata,
  void* array,
  struct brbt_bookkeeping_info* bk);

struct brbt_allocator_out
{
  void* data_array;
  struct brbt_bookkeeping_info* bk_array;
  unsigned size;
};

struct brbt_policy
{
  /* userdata to pass to the user policy functions */
  void* policy_data;

  /* hook function ran whenever a node is inserted */
  brbt_policy_insert_hook insert_hook;

  /* hook function ran whenever a node is removed */
  brbt_policy_remove_hook remove_hook;

  /* function that is called whenever a tree runs out of space
   * in the middle of an insert operation. this function is
   * allowed to return an empty allocation, which will
   * in turn cause the tree to call the "free" function to try
   * and remove a value from the tree to create space for the
   * inserted function. if free is non-existent or fails,
   * then the insert operation shall fail.
   */
  brbt_reallocator resize;
  brbt_free free;
  brbt_policy_select select;
};

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

  struct brbt_policy policy;
};

extern struct brbt_policy brbt_default_policy;

/* bk : pointer to an array of struct brbt_bookkeeping_info[capacity]
 *      if null, then an array will be maintained within the implementation
 * data: pointer to an array of your data structure of length capacity
 *      if null, then an array will be maintained within the implementation
 * key_offset: byte offset into your data structure for where the key is stored
 * policy: what to do when we run out of space
 * capacity: size of the policy of bk array, in logical size
 *      if null, the implementation will automatically resize the array
 */
struct brbt
brbt_create(size_t node_size,
            size_t key_offset,
            struct brbt_policy* policy,
            brbt_deleter deleter,
            brbt_comparator compare);

void
brbt_destroy(struct brbt*);

unsigned
brbt_size(struct brbt*);

unsigned
brbt_capacity(struct brbt*);

/* returns the node index for which the key is found
 * may return BRBT_NIL if no node matches
 */
brbt_node
brbt_find(struct brbt* tree, void const* key);

/* gets the node data associated with a node index */
void*
brbt_get(struct brbt* tree, brbt_node);

/* inserts a node with a key and returns its node index
 * if no delete operations are performed, then it is guaranteed
 *   that successive insert operations will return incrementing nodes indices
 */
brbt_node
brbt_insert(struct brbt* tree, void* node, bool replace);

/* deletes a node with a given key */
void
brbt_delete(struct brbt* tree, void* key);

/* empties a tree */
void
brbt_clear(struct brbt* tree);

/* deletes the smallest node within a subtree */
void
brbt_delete_min(struct brbt* tree, brbt_node);

void
brbt_iterate(struct brbt*, brbt_iterator, void* userdata);

/* returns the node index of the smallest node within the tree */
brbt_node
brbt_minimum(struct brbt* tree, brbt_node);

/* returns the node index to the root of the tree */
brbt_node
brbt_root(struct brbt* tree);

#ifndef BRBT_DEFAULT_POLICY
#include <stdlib.h>

static inline void
brbt_default_policy_free(struct brbt* tree,
                         void* user,
                         void* array,
                         struct brbt_bookkeeping_info* bk)
{
  (void)tree;
  (void)user;

  free(array);
  free(bk);
}

static inline struct brbt_allocator_out
brbt_default_policy_resize(struct brbt* tree,
                           void* user,
                           void* array,
                           struct brbt_bookkeeping_info* bk)
{
  (void)user;

  struct brbt_allocator_out out;
  unsigned old_cap = brbt_capacity(tree);
  unsigned new_cap = 0;
  if (old_cap == 0)
    new_cap = 64;
  else
    new_cap = old_cap * 1.5;

  out.bk_array = realloc(bk, sizeof *bk * new_cap);
  out.data_array = realloc(array, tree->member_bytesize * new_cap);
  out.size = new_cap;

  return out;
}

static inline struct brbt_policy
brbt_create_default_policy()
{
  struct brbt_policy out;
  out.policy_data = 0;
  out.insert_hook = 0;
  out.remove_hook = 0;
  out.resize = brbt_default_policy_resize;
  out.free = brbt_default_policy_free;
  out.select = 0;
  return out;
}

#endif
