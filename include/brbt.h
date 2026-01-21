#pragma once

#define BRBT_NIL ((unsigned)-1)

struct brbt;
typedef unsigned brbt_node;

enum brbt_allocation_request
{
  BRBT_GROW,
  BRBT_SHRINK,
};

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
    _Bool red;
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
typedef brbt_node (*brbt_policy_select)(struct brbt*);

/* hook to be ran whenever a node is inserted into the tree
 * allows one to maintain a bookkeeping list at an amortized cost
 */
typedef void (*brbt_policy_insert_hook)(struct brbt*, brbt_node);

/* hook to be ran whenever a node is removed from the tree
 * allows one to maintain a bookkeeping list at an amortized cost
 */
typedef void (*brbt_policy_remove_hook)(struct brbt*, brbt_node);

/* panic function called by brbt in case of internal failure
 * internal source line is provided for debugging/bug reports
 */
typedef void (*brbt_abort)(struct brbt*, int internal_source_line);

typedef void (*brbt_free)(struct brbt*);

typedef struct brbt_allocator_out (
  *brbt_reallocator)(struct brbt*, enum brbt_allocation_request);

struct brbt_allocator_out
{
  void* data_array;
  struct brbt_bookkeeping_info* bk_array;
  unsigned size;

  /* tells the implementation that the underlying
   * allocation was a realloc rather than a newly
   * allocated array */
  int realloc;
};

struct brbt_policy
{
  brbt_abort abort;

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

struct brbt_type
{
  /* member bytesize */
  unsigned membs;
  unsigned keyoff;
  brbt_comparator cmp;
  brbt_deleter deleter;
};

struct brbt
{
  /* realistically void*,
   * but the alignment shouldnt really matter
   * if it ever becomes a problem, make an issue
   */
  char* ptr;
  struct brbt_bookkeeping_info* bk;
  void* userdata;

  /* number of allocated nodes */
  unsigned size;

  /* maximum size of the array */
  unsigned capacity;

  /* index to the root node
   * if == BRBT_NIL, then no root is specified
   */
  brbt_node root;

  /* first free node in the list */
  brbt_node first_free;

  struct brbt_type const* _type;
  struct brbt_policy const* _policy;
};

#define brbt_usage(tree) ((tree).capacity * (tree)._type->membs)

/* expects tree to be a pointer */
#define brbt_for(tree, id, lambda)                                             \
  do {                                                                         \
    /* up to 2^32 node depth */                                                \
    unsigned _brbt_stack[32][2];                                               \
    unsigned _brbt_si = 0;                                                     \
    _brbt_stack[_brbt_si][0] = 0;                                              \
    _brbt_stack[_brbt_si++][1] = (tree)->root;                                 \
    while (_brbt_si > 0) {                                                     \
      unsigned (*_brbt_state)[2] = &_brbt_stack[_brbt_si - 1];                 \
      if ((*_brbt_state)[1] == BRBT_NIL) {                                     \
        _brbt_si--;                                                            \
        continue;                                                              \
      }                                                                        \
      if ((*_brbt_state)[0] == 0) {                                            \
        brbt_node lhs = brbt_left((tree), (*_brbt_state)[1]);                  \
        (*_brbt_state)[0] = 1;                                                 \
        _brbt_stack[_brbt_si][0] = 0;                                          \
        _brbt_stack[_brbt_si++][1] = lhs;                                      \
      } else {                                                                 \
        unsigned id = (*_brbt_state)[1];                                       \
        {                                                                      \
          lambda;                                                              \
        }                                                                      \
        (*_brbt_state)[1] = brbt_right((tree), (*_brbt_state)[1]);             \
        (*_brbt_state)[0] = 0;                                                 \
      }                                                                        \
    }                                                                          \
  } while (0)

struct brbt
brbt_create(struct brbt_type const* type,
            struct brbt_policy const* policy,
            void* userdata);

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
 * if no delete operations are performed, then it is guaranteed that
 * successive insert operations will return incrementing nodes indices
 */
brbt_node
brbt_insert(struct brbt* tree, void* node, _Bool replace);

/* deletes a node with a given key */
void
brbt_delete(struct brbt* tree, void* key);

/* empties a tree */
void
brbt_clear(struct brbt* tree);

/* deletes the smallest node within a subtree */
void
brbt_delete_min(struct brbt* tree, brbt_node);

/* returns the node index of the smallest node within the tree */
brbt_node
brbt_minimum(struct brbt* tree, brbt_node);

/* returns the node index to the root of the tree */
brbt_node
brbt_root(struct brbt* tree);

brbt_node
brbt_left(struct brbt*, brbt_node);
brbt_node
brbt_right(struct brbt*, brbt_node);

#ifndef BRBT_NO_STDLIB
#include <stdio.h>
#include <stdlib.h>

#ifndef BRBT_DEFAULT_CAPACITY
#define BRBT_DEFAULT_CAPACITY 64
#endif

static inline void
brbt_default_policy_free(struct brbt* tree)
{
  free(tree->ptr);
  free(tree->bk);
}

static inline struct brbt_allocator_out
brbt_default_policy_resize(struct brbt* tree, enum brbt_allocation_request req)
{
  struct brbt_allocator_out out;
  unsigned old_cap = brbt_capacity(tree);
  unsigned new_cap = 0;

  switch (req) {
    case BRBT_GROW:
      if (old_cap == 0)
        new_cap = BRBT_DEFAULT_CAPACITY;
      else
        new_cap = old_cap * 1.5;
      out.realloc = 1;
      break;

    case BRBT_SHRINK:
      tree->_policy->abort(tree, __LINE__);
      out.realloc = 0;
  }

  /* min of 32 */
  new_cap = (new_cap < 32) ? 32 : new_cap;

  out.bk_array =
    realloc(tree->bk, sizeof(struct brbt_bookkeeping_info) * new_cap);
  out.data_array = realloc(tree->ptr, tree->_type->membs * new_cap);
  out.size = new_cap;

  return out;
}

static inline void
brbt_default_abort(struct brbt* tree, int line_no)
{
  (void)tree;
  fprintf(stderr, "BRBT INTERNAL ABORT: line %i\n", line_no);
  abort();
}

static inline struct brbt_policy
brbt_create_default_policy()
{
  struct brbt_policy out;
  out.insert_hook = 0;
  out.remove_hook = 0;
  out.abort = brbt_default_abort;
  out.resize = brbt_default_policy_resize;
  out.free = brbt_default_policy_free;
  out.select = 0;
  return out;
}

#endif

/* helper combination function of find and get */
static inline void*
brbt_find_get(struct brbt* tree, void const* key)
{
  brbt_node idx = brbt_find(tree, key);
  if (idx == BRBT_NIL)
    return NULL;
  return brbt_get(tree, idx);
}
