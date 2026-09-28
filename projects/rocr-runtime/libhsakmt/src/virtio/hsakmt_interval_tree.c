/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "hsakmt_interval_tree.h"
#include <stddef.h>

static inline interval_tree_node_t* interval_tree_entry(rbtree_node_t* rb) {
  return (interval_tree_node_t*)((char*)rb - offsetof(interval_tree_node_t, rb));
}

void hsakmt_interval_tree_insert(interval_tree_t* tree, interval_tree_node_t* node) {
  hsakmt_rbtree_insert(tree, &node->rb);
}

void hsakmt_interval_tree_remove(interval_tree_t* tree, interval_tree_node_t* node) {
  hsakmt_rbtree_delete(tree, &node->rb);
}

static interval_tree_node_t* interval_tree_scan(interval_tree_t* tree, rbtree_node_t* rb,
                                                unsigned long start, unsigned long last) {
  while (rb) {
    interval_tree_node_t* node = interval_tree_entry(rb);
    if (node->start > last) return NULL;
    if (node->last >= start) return node;
    rb = hsakmt_rbtree_next(tree, rb);
  }
  return NULL;
}

interval_tree_node_t* hsakmt_interval_tree_iter_first(interval_tree_t* tree, unsigned long start,
                                                      unsigned long last) {
  return interval_tree_scan(tree, rbtree_min_max(tree, LEFT), start, last);
}

interval_tree_node_t* hsakmt_interval_tree_iter_next(interval_tree_t* tree,
                                                     interval_tree_node_t* node,
                                                     unsigned long start, unsigned long last) {
  return interval_tree_scan(tree, hsakmt_rbtree_next(tree, &node->rb), start, last);
}
