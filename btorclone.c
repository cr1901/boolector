/*  Boolector: Satisfiablity Modulo Theories (SMT) solver.
 *
 *  Copyright (C) 2013 Aina Niemetz.
 *
 *  All rights reserved.
 *
 *  This file is part of Boolector.
 *  See COPYING for more information on using this software.
 */

#include "btoraig.h"
#include "btoraigvec.h"
#include "btorexp.h"
#include "btorhash.h"
#include "btormap.h"
#include "btorstack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

BtorNode *
clone_exp (Btor *clone,
           BtorNode *exp,
           BtorNodePtrPtrStack *parents,
           BtorNodePtrPtrStack *chnexts,
           BtorNodeMap *exp_map,
           BtorAIGMap *aig_map)
{
  assert (clone);
  assert (exp);
  assert (parents);
  assert (chnexts);
  assert (exp_map);
  assert (aig_map);

  int i, len;
  BtorNode *res, *real_exp;
  BtorMemMgr *mm;

  mm = clone->mm;

  real_exp = BTOR_REAL_ADDR_NODE (exp);
  assert (!BTOR_IS_PROXY_NODE (real_exp));

  res = btor_malloc (mm, real_exp->bytes);
  memcpy (res, real_exp, real_exp->bytes);

  if (real_exp->bits)
  {
    len = strlen (real_exp->bits);
    BTOR_NEWN (mm, res->bits, len);
    for (i = 0; i < len; i++) res->bits[i] = real_exp->bits[i];
  }

  /* Note: no need to cache aig vectors here (exp->av is unique to exp). */
  if (!BTOR_IS_ARRAY_NODE (real_exp) && real_exp->av)
    res->av = btor_clone_aigvec (real_exp->av, clone->avmgr, aig_map);
  /* else: no need to clone rho (valid only during consistency checking). */

  BTOR_PUSH_STACK_IF (real_exp->next, mm, *chnexts, &res->next);
  BTOR_PUSH_STACK_IF (real_exp->parent, mm, *parents, &res->parent);

  assert (!real_exp->simplified);

  res->btor = clone;

  BTOR_PUSH_STACK_IF (real_exp->first_parent, mm, *parents, &res->first_parent);
  BTOR_PUSH_STACK_IF (real_exp->last_parent, mm, *parents, &res->last_parent);

  if (!BTOR_IS_BV_CONST_NODE (real_exp) && !BTOR_IS_BV_VAR_NODE (real_exp)
      && !BTOR_IS_ARRAY_VAR_NODE (real_exp) && !BTOR_IS_PARAM_NODE (real_exp))
  {
    if (real_exp->arity)
    {
      for (i = 0; i < real_exp->arity; i++)
      {
        res->e[i] = btor_mapped_node (exp_map, real_exp->e[i]);
        assert (res->e[i]);
      }
    }
    else
    {
      res->symbol = btor_strdup (mm, real_exp->symbol);
      if (BTOR_IS_ARRAY_EQ_NODE (real_exp) && real_exp->vreads)
      {
        assert (btor_mapped_node (exp_map, real_exp->vreads->exp1));
        assert (btor_mapped_node (exp_map, real_exp->vreads->exp2));
        res->vreads =
            new_exp_pair (clone,
                          btor_mapped_node (exp_map, real_exp->vreads->exp1),
                          btor_mapped_node (exp_map, real_exp->vreads->exp2));
      }
    }

    for (i = 0; i < real_exp->arity; i++)
    {
      BTOR_PUSH_STACK_IF (
          real_exp->prev_parent[i], mm, *parents, &res->prev_parent[i]);
      BTOR_PUSH_STACK_IF (
          real_exp->next_parent[i], mm, *parents, &res->next_parent[i]);
    }
  }

  if (BTOR_IS_ARRAY_NODE (real_exp))
  {
    BTOR_PUSH_STACK_IF (real_exp->first_aeq_acond_parent,
                        mm,
                        *parents,
                        &res->first_aeq_acond_parent);
    BTOR_PUSH_STACK_IF (real_exp->last_aeq_acond_parent,
                        mm,
                        *parents,
                        &res->last_aeq_acond_parent);

    if (!BTOR_IS_ARRAY_VAR_NODE (real_exp))
    {
      for (i = 0; i < real_exp->arity; i++)
      {
        BTOR_PUSH_STACK_IF (real_exp->prev_aeq_acond_parent[i],
                            mm,
                            *parents,
                            &res->prev_aeq_acond_parent[i]);
        BTOR_PUSH_STACK_IF (real_exp->next_aeq_acond_parent[i],
                            mm,
                            *parents,
                            &res->next_aeq_acond_parent[i]);
      }
    }
  }

  res = BTOR_IS_INVERTED_NODE (exp) ? BTOR_INVERT_NODE (res) : res;
  btor_map_node (exp_map, exp, res);

  return res;
}

static void
clone_nodes_id_table (Btor *clone,
                      BtorNodePtrStack *id_table,
                      BtorNodePtrStack *res_id_table,
                      BtorNodePtrPtrStack *parents,
                      BtorNodePtrPtrStack *chnexts,
                      BtorAIGPtrPtrStack *aigs,
                      BtorNodeMap *exp_map,
                      BtorAIGMap *aig_map)
{
  assert (id_table);
  assert (res_id_table);
  assert (parents);
  assert (chnexts);
  assert (aigs);
  assert (exp_map);
  assert (aig_map);

  int i, n, tag;
  BtorAIG **next;
  BtorNode **tmp;
  BtorMemMgr *mm;

  mm = clone->mm;

  n = BTOR_COUNT_STACK (*id_table);
  /* first element (id = 0) is empty */
  for (i = 1; i < n; i++)
    BTOR_PUSH_STACK (
        mm,
        *res_id_table,
        id_table->start[i]
            ? clone_exp (
                  clone, id_table->start[i], parents, chnexts, exp_map, aig_map)
            : id_table->start[i]);

  /* update children, parent and next pointers of expressions */
  while (!BTOR_EMPTY_STACK (*chnexts))
  {
    tmp = BTOR_POP_STACK (*chnexts);
    assert (*tmp);
    *tmp = btor_mapped_node (exp_map, *tmp);
    assert (*tmp);
  }
  while (!BTOR_EMPTY_STACK (*parents))
  {
    tmp = BTOR_POP_STACK (*parents);
    assert (*tmp);
    tag  = BTOR_GET_TAG_NODE (*tmp);
    *tmp = btor_mapped_node (exp_map, BTOR_REAL_ADDR_NODE (*tmp));
    assert (*tmp);
    *tmp = BTOR_TAG_NODE (*tmp, tag);
  }

  /* update next pointers of aigs */
  while (!BTOR_EMPTY_STACK (*aigs))
  {
    next = BTOR_POP_STACK (*aigs);
    assert (*next);
    *next = btor_mapped_aig (aig_map, *next);
    assert (*next);
  }
}

static void
clone_nodes_unique_table (BtorMemMgr *mm,
                          BtorNodeUniqueTable *table,
                          BtorNodeUniqueTable *res,
                          BtorNodeMap *exp_map)
{
  assert (mm);
  assert (table);
  assert (res);
  assert (exp_map);

  int i;

  BTOR_CNEWN (mm, res->chains, table->size);
  res->size         = table->size;
  res->num_elements = table->num_elements;

  for (i = 0; i < table->size; i++)
  {
    if (!table->chains[i]) continue;
    res->chains[i] = btor_mapped_node (exp_map, table->chains[i]);
    // TODO debug
    if (res->chains[i]->id == 71)
      printf ("** res->chains[i] %p\n", res->chains[i]);
    //
    assert (res->chains[i]);
  }
}

static void
clone_node_ptr_stack (BtorMemMgr *mm,
                      BtorNodePtrStack *stack,
                      BtorNodePtrStack *res_stack,
                      BtorNodeMap *exp_map)
{
  assert (stack);
  assert (res_stack);
  assert (exp_map);

  int i;
  BtorNode *cloned_exp;

  for (i = 0; i < BTOR_COUNT_STACK (*stack); i++)
  {
    if ((*stack).start[i]) /* Note: first element of nodes id table is 0 */
    {
      cloned_exp = btor_mapped_node (exp_map, (*stack).start[i]);
      assert (cloned_exp);
    }
    BTOR_PUSH_STACK (mm, *res_stack, cloned_exp);
  }
}

static void *
mapped_node (const void *map, const void *key)
{
  assert (map);
  assert (key);

  BtorNode *exp, *cloned_exp;
  BtorNodeMap *exp_map;

  exp        = (BtorNode *) key;
  exp_map    = (BtorNodeMap *) map;
  cloned_exp = btor_mapped_node (exp_map, exp);
  assert (cloned_exp);
  return cloned_exp;
}

static void
data_as_node_ptr (const void *map, const void *key, BtorPtrHashData *data)
{
  assert (map);
  assert (key);
  assert (data);

  BtorNode *exp, *cloned_exp;
  BtorNodeMap *exp_map;

  exp        = (BtorNode *) key;
  exp_map    = (BtorNodeMap *) map;
  cloned_exp = btor_mapped_node (exp_map, exp);
  assert (cloned_exp);
  data->asPtr = cloned_exp;
}

#define CLONE_PTR_HASH_TABLE(table)                   \
  do                                                  \
  {                                                   \
    clone->table = btor_clone_ptr_hash_table (        \
        mm, btor->table, mapped_node, 0, exp_map, 0); \
  } while (0)

#define CLONE_PTR_HASH_TABLE_ASPTR(table)                                  \
  do                                                                       \
  {                                                                        \
    clone->table = btor_clone_ptr_hash_table (                             \
        mm, btor->table, mapped_node, data_as_node_ptr, exp_map, exp_map); \
  } while (0)

Btor *
btor_clone_btor (Btor *btor)
{
  assert (btor);

  Btor *clone;
  BtorNodeMap *exp_map;
  BtorAIGMap *aig_map;
  BtorNodePtrPtrStack parents, chnexts;
  BtorAIGPtrPtrStack aigs;

  BtorMemMgr *mm;

  BTOR_INIT_STACK (parents);
  BTOR_INIT_STACK (chnexts);
  BTOR_INIT_STACK (aigs);

  exp_map = btor_new_node_map (btor);
  aig_map = btor_new_aig_map (btor);

  mm = btor_new_mem_mgr ();
  BTOR_CNEW (mm, clone);
  clone->mm = mm;

  memcpy (&clone->bv_lambda_id,
          &btor->bv_lambda_id,
          (char *) &btor->lod_cache - (char *) &btor->bv_lambda_id);
  memcpy (&clone->stats,
          &btor->stats,
          (char *) btor + sizeof (*btor) - (char *) &btor->stats);

  clone->avmgr = btor_clone_aigvec_mgr (btor->avmgr, mm, &aigs, aig_map);

  clone_nodes_id_table (clone,
                        &btor->nodes_id_table,
                        &clone->nodes_id_table,
                        &parents,
                        &chnexts,
                        &aigs,
                        exp_map,
                        aig_map);

  clone_nodes_unique_table (
      mm, &btor->nodes_unique_table, &clone->nodes_unique_table, exp_map);

  // TODO sorts_unique_table (currently unused)

  CLONE_PTR_HASH_TABLE (bv_vars);
  CLONE_PTR_HASH_TABLE (array_vars);
  CLONE_PTR_HASH_TABLE (lambdas);
  CLONE_PTR_HASH_TABLE_ASPTR (substitutions);
  CLONE_PTR_HASH_TABLE (lod_cache);
  CLONE_PTR_HASH_TABLE_ASPTR (varsubst_constraints);
  CLONE_PTR_HASH_TABLE (embedded_constraints);
  CLONE_PTR_HASH_TABLE (unsynthesized_constraints);
  CLONE_PTR_HASH_TABLE (synthesized_constraints);
  CLONE_PTR_HASH_TABLE (assumptions);
  CLONE_PTR_HASH_TABLE (var_rhs);
  CLONE_PTR_HASH_TABLE (array_rhs);

  clone_node_ptr_stack (
      clone->mm, &btor->arrays_with_model, &clone->arrays_with_model, exp_map);

  CLONE_PTR_HASH_TABLE_ASPTR (cache);

  clone->clone         = NULL;
  clone->apitrace      = NULL;
  clone->closeapitrace = 0;

  btor_delete_node_map (exp_map);
  btor_delete_aig_map (aig_map);

  BTOR_RELEASE_STACK (mm, parents);
  BTOR_RELEASE_STACK (mm, chnexts);
  BTOR_RELEASE_STACK (mm, aigs);

  return clone;
}
