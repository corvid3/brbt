#pragma once

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>

#define BRBT_NA ((unsigned)-1)

struct brbt_tree;
typedef unsigned node_idx;

enum brbt_node_flags : unsigned char
{
  BRBT_NODE_FREE = 0b01,
  BRBT_NODE_RED = 0b10,
};

struct brbt_bookkeeping_info
{
  node_idx left, right;
  node_idx parent;
  node_idx next_free;
  bool red;
};

/*
 * NOTE: do not pass any data structure that is aligned to
 *  greater than max_align_t to brbt!
 */

typedef void (*brbt_iterator)(struct brbt_tree*, void* userdata, node_idx);

typedef int (*brbt_comparator)(void* key_lhs, void* key_rhs);
typedef void (*brbt_deleter)(struct brbt_tree*, node_idx);

/* function initially called when the internal array of a tree is
 * filled, and must make space for a newly inserted node
 * the policy_data pointer specified in brbt_policy will be passed as userdata
 */
typedef void (*brbt_policy_begin)(void* userdata);

/* function to be ran once per node slot in the binary trees internal array
 * the policy_data pointer specified in brbt_policy will be passed as userdata
 * node is a pointer to the userdata pointer of a specific node
 * node_idx is the index into the internal array of said node
 * returning true will immediately end the iteration and call brbt_policy_decide
 * returning false will continue the iteration
 */
typedef bool (*brbt_policy_run)(void* userdata, node_idx);

/* the function ran either after the brbt_policy_run function returns true,
 * or after every node index has been iterated over.
 * the policy_data pointer specified in brbt_policy will be passed as userdata
 * the function must return a valid node index that will be removed
 */
typedef size_t (*brbt_policy_decide)(void* userdata);

/* hook to be ran whenever a node is inserted into the tree
 * allows one to maintain a bookkeeping list at an amortized cost
 */
typedef size_t (*brbt_policy_insert_hook)(void* userdata, node_idx);

/* hook to be ran whenever a node is removed from the tree
 * allows one to maintain a bookkeeping list at an amortized cost
 */
typedef size_t (*brbt_policy_remove_hook)(void* userdata, node_idx);

/* data structure to call for when the tree is full
 * but a node must be inserted, therefore a node must be deleted
 */
struct brbt_policy
{
  void* policy_data;

  /* nullable */
  brbt_policy_run run;

  /* not nullable */
  brbt_policy_begin begin;
  brbt_policy_decide decide;

  /* nullable */
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
struct brbt_tree*
brbt_create_ex(size_t node_size,
               size_t capacity,
               size_t key_offset,
               struct brbt_policy* policy,
               struct brbt_bookkeeping_info* bk,
               void* data,
               brbt_deleter deleter,
               brbt_comparator compare);

/* returns the node index for which the key is found
 * may return BRBT_NA if no node matches
 */
node_idx
brbt_find(struct brbt_tree* tree, void* key);

/* gets the node data associated with a node index */
void*
brbt_get(struct brbt_tree* tree, node_idx);

/* inserts a node with a key and returns its node index */
node_idx
brbt_insert(struct brbt_tree* tree, void* node, bool replace);

/* deletes a node with a given key */
void
brbt_delete(struct brbt_tree* tree, void* key);

/* deletes the smallest node within a subtree */
void
brbt_delete_min(struct brbt_tree* tree, node_idx);

void
brbt_iterate(struct brbt_tree*, brbt_iterator, void* userdata);

/* returns the node index of the smallest node within the tree */
node_idx
brbt_minimum(struct brbt_tree* tree, node_idx);

/* returns the node index to the root of the tree */
node_idx
brbt_root(struct brbt_tree* tree);
