/*
    $Id: data_structures.h,v 1.1 2003/10/13 11:47:12 f1rmb Exp $

    Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __VCD_DATA_STRUCTURES_H__
#define __VCD_DATA_STRUCTURES_H__

#include <libvcd/types.h>

/* opaque... */

typedef int (*_vcd_list_cmp_func) (void *data1, void *data2);

typedef int (*_vcd_list_iterfunc) (void *data, void *user_data);

/* methods */
VcdList *_vcd_list_new (void);

void _vcd_list_free (VcdList *list, int free_data);

unsigned _vcd_list_length (const VcdList *list);

void _vcd_list_sort (VcdList *list, _vcd_list_cmp_func cmp_func);

void _vcd_list_prepend (VcdList *list, void *data);

void _vcd_list_append (VcdList *list, void *data);

void _vcd_list_foreach (VcdList *list, _vcd_list_iterfunc func, void *user_data);

VcdListNode *_vcd_list_find (VcdList *list, _vcd_list_iterfunc cmp_func, void *user_data);

#define _VCD_LIST_FOREACH(node, list) \
 for (node = _vcd_list_begin (list); node; node = _vcd_list_node_next (node))

/* node ops */

VcdListNode *_vcd_list_at (VcdList *list, int idx);

VcdListNode *_vcd_list_begin (const VcdList *list);

VcdListNode *_vcd_list_end (VcdList *list);

VcdListNode *_vcd_list_node_next (VcdListNode *node);

void _vcd_list_node_free (VcdListNode *node, int free_data);

void *_vcd_list_node_data (VcdListNode *node);


/* n-way tree */

typedef struct _VcdTree VcdTree;
typedef struct _VcdTreeNode VcdTreeNode;

#define _VCD_CHILD_FOREACH(child, parent) \
 for (child = _vcd_tree_node_first_child (parent); child; child = _vcd_tree_node_next_sibling (child))

typedef int (*_vcd_tree_node_cmp_func) (VcdTreeNode *node1, 
                                        VcdTreeNode *node2);

typedef void (*_vcd_tree_node_traversal_func) (VcdTreeNode *node, 
                                               void *user_data);

VcdTree *_vcd_tree_new (void *root_data);

void _vcd_tree_destroy (VcdTree *tree, bool free_data);

VcdTreeNode *_vcd_tree_root (VcdTree *tree);

void _vcd_tree_node_sort_children (VcdTreeNode *node,
                                   _vcd_tree_node_cmp_func cmp_func);

void *_vcd_tree_node_data (VcdTreeNode *node);

void _vcd_tree_node_destroy (VcdTreeNode *node, bool free_data);

void *_vcd_tree_node_set_data (VcdTreeNode *node, void *new_data);

VcdTreeNode *_vcd_tree_node_append_child (VcdTreeNode *pnode, void *cdata);

VcdTreeNode *_vcd_tree_node_first_child (VcdTreeNode *node);

VcdTreeNode *_vcd_tree_node_next_sibling (VcdTreeNode *node);

VcdTreeNode *_vcd_tree_node_parent (VcdTreeNode *node);

VcdTreeNode *_vcd_tree_node_root (VcdTreeNode *node);

bool _vcd_tree_node_is_root (VcdTreeNode *node);

void _vcd_tree_node_traverse (VcdTreeNode *node, 
                              _vcd_tree_node_traversal_func trav_func,
                              void *user_data);

void
_vcd_tree_node_traverse_bf (VcdTreeNode *node, 
                            _vcd_tree_node_traversal_func trav_func,
                            void *user_data);
     
#endif /* __VCD_DATA_STRUCTURES_H__ */

/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */

