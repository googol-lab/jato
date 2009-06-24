/*
 * Copyright (C) 2006  Pekka Enberg
 *
 * This file is released under the GPL version 2. Please refer to the file
 * LICENSE for details.
 * 
 * This file contains functions for basic block operations.
 */

#include <jit/basic-block.h>
#include <jit/compilation-unit.h>
#include <jit/statement.h>

#include <arch/instruction.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <vm/buffer.h>

struct basic_block *alloc_basic_block(struct compilation_unit *b_parent, unsigned long start, unsigned long end)
{
	struct basic_block *bb;

	bb = malloc(sizeof(*bb));
	if (!bb)
		return NULL;
	memset(bb, 0, sizeof(*bb));

	bb->mimic_stack = alloc_stack();
	if (!bb->mimic_stack) {
		free(bb);
		return NULL;
	}
	INIT_LIST_HEAD(&bb->stmt_list);
	INIT_LIST_HEAD(&bb->insn_list);
	INIT_LIST_HEAD(&bb->backpatch_insns);
	INIT_LIST_HEAD(&bb->bb_list_node);
	bb->b_parent = b_parent;
	bb->start = start;
	bb->end = end;

	return bb;
}

struct basic_block *get_basic_block(struct compilation_unit *cu, unsigned long start, unsigned long end)
{
	struct basic_block *bb;

	bb = alloc_basic_block(cu, start, end);
	if (bb)
		list_add_tail(&bb->bb_list_node, &cu->bb_list);
	return bb;
}

static void free_stmt_list(struct list_head *head)
{
	struct statement *stmt, *tmp;

	list_for_each_entry_safe(stmt, tmp, head, stmt_list_node)
		free_statement(stmt);
}

static void free_insn_list(struct list_head *head)
{
	struct insn *insn, *tmp;

	list_for_each_entry_safe(insn, tmp, head, insn_list_node)
		free_insn(insn);
}

void free_basic_block(struct basic_block *bb)
{
	free_stack(bb->mimic_stack);
	free_stmt_list(&bb->stmt_list);
	free_insn_list(&bb->insn_list);
	free(bb->successors);
	free(bb->predecessors);
	free(bb->use_set);
	free(bb->def_set);
	free(bb->live_in_set);
	free(bb->live_out_set);
	free(bb);
}

/**
 *	bb_split - Split basic block into two.
 * 	@orig_bb: Basic block to split.
 * 	@offset: The end offset of the upper basic block and start offset
 *		of the bottom basic block.
 *
 * 	Splits the basic block into two parts and returns a pointer to the
 * 	newly allocated block. The end offset of the given basic block is
 * 	updated accordingly. Successors of the both block is also updated.
 */
struct basic_block *bb_split(struct basic_block *orig_bb, unsigned long offset)
{
	struct basic_block *new_bb;

	if (offset < orig_bb->start || offset >= orig_bb->end)
		return NULL;

	new_bb = get_basic_block(orig_bb->b_parent, offset, orig_bb->end);
	if (new_bb == NULL)
		return NULL;

	orig_bb->end = offset;

	new_bb->successors = orig_bb->successors;
	orig_bb->successors = NULL;

	new_bb->nr_successors = orig_bb->nr_successors;
	orig_bb->nr_successors = 0;

	new_bb->predecessors = NULL;
	new_bb->nr_predecessors = 0;

	if (orig_bb->has_branch) {
		orig_bb->has_branch = false;
		new_bb->has_branch = true;

		new_bb->br_target_off = orig_bb->br_target_off;
		orig_bb->br_target_off = 0;
	}

	return new_bb;
}

void bb_add_stmt(struct basic_block *bb, struct statement *stmt)
{
	list_add_tail(&stmt->stmt_list_node, &bb->stmt_list);
}

struct statement *bb_remove_last_stmt(struct basic_block *bb)
{
	struct list_head *last = list_last(&bb->stmt_list);

	list_del(last);

	return list_entry(last, struct statement, stmt_list_node);
}

void bb_add_insn(struct basic_block *bb, struct insn *insn)
{
	list_add_tail(&insn->insn_list_node, &bb->insn_list);
}

int __bb_add_neighbor(struct basic_block *new, struct basic_block ***array, unsigned long *nb)
{
	unsigned long new_size;
	struct basic_block **new_neighbors;

	new_size = sizeof(struct basic_block *) * (*nb + 1);

	new_neighbors = realloc(*array, new_size);
	if (new_neighbors == NULL)
		return -ENOMEM;

	*array = new_neighbors;

	(*array)[*nb] = new;
	(*nb)++;

	return 0;
}

int bb_add_successor(struct basic_block *bb, struct basic_block *successor)
{
	__bb_add_neighbor(bb, &successor->predecessors, &successor->nr_predecessors);
	return __bb_add_neighbor(successor, &bb->successors, &bb->nr_successors);
}

int bb_add_predecessor(struct basic_block *bb, struct basic_block *predecessor)
{
	return __bb_add_neighbor(predecessor, &bb->predecessors, &bb->nr_predecessors);
}

unsigned char *bb_native_ptr(struct basic_block *bb)
{
	return buffer_ptr(bb->b_parent->objcode) + bb->mach_offset;
}
