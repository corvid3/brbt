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

typedef int (*brbt_comparator)(void* key_lhs, void* key_rhs);
typedef void (*brbt_deleter)(struct brbt*, brbt_node);

/* function ran when the internal tree becomes full, and
 * the array is defined by the user, therefore the array may not be
 * reallocated.
 * the function must return a valid node index to which node should be freed
 */
typedef brbt_node (*brbt_policy_free)(struct brbt*, void* userdata);

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

/* data structure to call for when the tree is full
 * but a node must be inserted, therefore a node must be deleted
 */
struct brbt_policy
{
  void* policy_data;

  /* nullable */
  brbt_policy_free free;
  brbt_policy_insert_hook insert_hook;
  brbt_policy_remove_hook remove_hook;
};

/* bk : pointer to an array of struct brbt_bookkeeping_info[capacity]
 *      if null, then an array will be maintained within the implementation
 * data: pointer to an array of your data structure of length capacity
 *      if null, then an array will be maintained within the implementation
 * key_offset: byte offset into your data structure for where the key is stored
 * policy: what to do when we run out of space
 *      if null, tree will fail whenever a new node is inserted
 * capacity: size of the policy of bk array, in logical size
 *      if null, the implementation will automatically resize the array
 */
struct brbt*
brbt_create(size_t node_size,
            size_t capacity,
            size_t key_offset,
            struct brbt_policy* policy,
            struct brbt_bookkeeping_info* bk,
            void* data,
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
brbt_find(struct brbt* tree, void* key);

/* gets the node data associated with a node index */
void*
brbt_get(struct brbt* tree, brbt_node);

/* inserts a node with a key and returns its node index */
brbt_node
brbt_insert(struct brbt* tree, void* node, bool replace);

/* deletes a node with a given key */
void
brbt_delete(struct brbt* tree, void* key);

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
