/*  Boolector: Satisfiablity Modulo Theories (SMT) solver.
 *
 *  Copyright (C) 2014-2016 Mathias Preiner.
 *  Copyright (C) 2014-2016 Aina Niemetz.
 *
 *  All rights reserved.
 *
 *  This file is part of Boolector.
 *  See COPYING for more information on using this software.
 */

#include "btorcore.h"
#include "btordbg.h"
#include "btorexp.h"
#include "btorslvfun.h"
#include "utils/btorhashint.h"
#include "utils/btorhashptr.h"
#include "utils/btorutil.h"

/* heuristic: minimum depth to the inputs
 *            (considering the whole formula or the bv skeleton, only) */
static void
compute_scores_aux_min_dep (Btor *btor, BtorNodePtrStack *nodes)
{
  assert (btor);
  assert (BTOR_FUN_SOLVER (btor)->score);
  assert (nodes);

  int i, j, min_depth;
  BtorFunSolver *slv;
  BtorNodePtrStack stack;
  BtorNode *cur, *e;
  BtorPtrHashTable *score;
  BtorPtrHashBucket *b;
  BtorIntHashTable *mark;
  BtorHashTableData *d;
  BtorMemMgr *mm;

  mm = btor->mm;
  BTOR_INIT_STACK (mm, stack);

  slv   = BTOR_FUN_SOLVER (btor);
  score = slv->score;
  mark  = btor_hashint_map_new (mm);

  for (j = 0; j < BTOR_COUNT_STACK (*nodes); j++)
  {
    cur = BTOR_PEEK_STACK (*nodes, j);
    BTOR_PUSH_STACK (stack, cur);
    while (!BTOR_EMPTY_STACK (stack))
    {
      cur = BTOR_REAL_ADDR_NODE (BTOR_POP_STACK (stack));
      d   = btor_hashint_map_get (mark, cur->id);

      if (d && d->as_int == 1) continue;

      if (!d)
      {
        d = btor_hashint_map_add (mark, cur->id);
        BTOR_PUSH_STACK (stack, cur);

        if (cur->arity == 0)
        {
          if (!(b = btor_hashptr_table_get (score, cur)))
            b = btor_hashptr_table_add (score, btor_copy_exp (btor, cur));
          b->data.as_int = 1;
          d->as_int      = 1;
          continue;
        }

        for (i = 0; i < cur->arity; i++) BTOR_PUSH_STACK (stack, cur->e[i]);
      }
      else
      {
        assert (d->as_int == 0);
        assert (cur->arity > 0);
        assert (!btor_is_uf_node (cur));
        d->as_int = 1;

        min_depth = -1;
        for (i = 0; i < cur->arity; i++)
        {
          e = BTOR_REAL_ADDR_NODE (cur->e[i]);
          b = btor_hashptr_table_get (score, e);
          assert (b);
          if (min_depth == -1 || b->data.as_int < min_depth)
            min_depth = b->data.as_int;
        }
        assert (min_depth >= 0);
        if (!(b = btor_hashptr_table_get (score, cur)))
          b = btor_hashptr_table_add (score, btor_copy_exp (btor, cur));
        b->data.as_int = min_depth + 1;
      }
    }
  }

  BTOR_RELEASE_STACK (stack);
  btor_hashint_map_delete (mark);
}

/* heuristic: minimum number of unique applies on a path to the inputs
 *            (considering the whole formula, or the bv skeleton only) */
static void
compute_scores_aux_min_app (Btor *btor, BtorNodePtrStack *nodes)
{
  assert (btor);
  assert (BTOR_FUN_SOLVER (btor)->score);
  assert (nodes);

  double delta;
  int i, j, k;
  BtorFunSolver *slv;
  BtorNode *cur, *e;
  BtorNodePtrStack stack;
  BtorPtrHashTableIterator it;
  BtorPtrHashBucket *b;
  BtorPtrHashTable *in, *t, *min_t;
  BtorIntHashTable *mark;
  BtorMemMgr *mm;

  mm = btor->mm;
  BTOR_INIT_STACK (mm, stack);

  slv = BTOR_FUN_SOLVER (btor);

  qsort (nodes->start,
         BTOR_COUNT_STACK (*nodes),
         sizeof (BtorNode *),
         btor_compare_exp_by_id_qsort_asc);

  /* compute score */
  for (k = 0; k < BTOR_COUNT_STACK (*nodes); k++)
  {
    cur = BTOR_PEEK_STACK (*nodes, k);
    b   = btor_hashptr_table_get (slv->score, cur);
    assert (b);
    assert (!b->data.as_ptr);
    b->data.as_ptr =
        btor_hashptr_table_new (mm,
                                (BtorHashPtr) btor_hash_exp_by_id,
                                (BtorCmpPtr) btor_compare_exp_by_id);
    in = b->data.as_ptr;

    if (!cur->parameterized && btor_is_and_node (cur))
    {
      /* choose min path */
      min_t = 0;
      for (i = 0; i < cur->arity; i++)
      {
        e = BTOR_REAL_ADDR_NODE (cur->e[i]);
        b = btor_hashptr_table_get (slv->score, e);
        assert (b);
        t = (BtorPtrHashTable *) b->data.as_ptr;
        assert (t);
        if (!min_t || t->count < min_t->count) min_t = t;
      }
      assert (min_t);
      btor_iter_hashptr_init (&it, min_t);
      while (btor_iter_hashptr_has_next (&it))
      {
        e = btor_iter_hashptr_next (&it);
        assert (!btor_hashptr_table_get (in, e));
        btor_hashptr_table_add (in, btor_copy_exp (btor, e));
      }
    }
    else
    {
      for (i = 0; i < cur->arity; i++)
      {
        e = BTOR_REAL_ADDR_NODE (cur->e[i]);
        b = btor_hashptr_table_get (slv->score, e);
        if (b && (t = b->data.as_ptr))
        {
          /* merge tables */
          delta = btor_util_time_stamp ();
          btor_iter_hashptr_init (&it, t);
          while (btor_iter_hashptr_has_next (&it))
          {
            e = btor_iter_hashptr_next (&it);
            if (!btor_hashptr_table_get (in, e))
              btor_hashptr_table_add (in, btor_copy_exp (btor, e));
          }
          slv->time.search_init_apps_compute_scores_merge_applies +=
              btor_util_time_stamp () - delta;
        }
        else
        {
          mark = btor_hashint_table_new (mm);
          /* search unique applies */
          BTOR_PUSH_STACK (stack, e);
          while (!BTOR_EMPTY_STACK (stack))
          {
            e = BTOR_REAL_ADDR_NODE (BTOR_POP_STACK (stack));
            if (btor_hashint_table_contains (mark, e->id)) continue;
            btor_hashint_table_add (mark, e->id);
            if (!e->parameterized && btor_is_apply_node (e)
                && !btor_hashptr_table_get (in, e))
              btor_hashptr_table_add (in, btor_copy_exp (btor, e));
            for (j = 0; j < e->arity; j++) BTOR_PUSH_STACK (stack, e->e[j]);
          }
          btor_hashint_table_delete (mark);
        }
      }
    }
  }

  BTOR_RELEASE_STACK (stack);
}

static void
compute_scores_aux (Btor *btor, BtorNodePtrStack *nodes)
{
  assert (BTOR_FUN_SOLVER (btor)->score);

  int h;

  h = btor_opt_get (btor, BTOR_OPT_FUN_JUST_HEURISTIC);
  if (h == BTOR_JUST_HEUR_BRANCH_MIN_APP)
    compute_scores_aux_min_app (btor, nodes);
  else if (h == BTOR_JUST_HEUR_BRANCH_MIN_DEP)
    compute_scores_aux_min_dep (btor, nodes);
  else /* no scores required for BTOR_JUST_HEUR_LEFT */
    assert (h == BTOR_JUST_HEUR_LEFT);
}

void
btor_compute_scores (Btor *btor)
{
  assert (btor);

  int i;
  double start;
  BtorFunSolver *slv;
  BtorNode *cur, *e;
  BtorPtrHashTableIterator it;
  BtorNodePtrStack stack, nodes;
  BtorIntHashTable *mark;
  BtorMemMgr *mm;

  /* computing scores only required for BTOR_JUST_HEUR_BRANCH_MIN_DEP and
   * BTOR_JUST_HEUR_BRANCH_MIN_APP */
  if (btor_opt_get (btor, BTOR_OPT_FUN_JUST_HEURISTIC) == BTOR_JUST_HEUR_LEFT)
    return;

  /* Collect all nodes we actually need the score for.  If just is enabled, we
   * only need the children of AND nodes. If dual prop is enabled, we only need
   * APPLY nodes (BV var nodes always have score 0 or 1 depending on the
   * selected heuristic and are treated as such in compare_scores).
   * -> see btor_compute_scores_dual_prop */

  start = btor_util_time_stamp ();
  mm    = btor->mm;
  BTOR_INIT_STACK (mm, stack);
  BTOR_INIT_STACK (mm, nodes);
  mark = btor_hashint_table_new (mm);

  slv = BTOR_FUN_SOLVER (btor);

  if (!slv->score)
    slv->score = btor_hashptr_table_new (mm,
                                         (BtorHashPtr) btor_hash_exp_by_id,
                                         (BtorCmpPtr) btor_compare_exp_by_id);

  btor_iter_hashptr_init (&it, btor->synthesized_constraints);
  btor_iter_hashptr_queue (&it, btor->assumptions);
  while (btor_iter_hashptr_has_next (&it))
  {
    cur = btor_iter_hashptr_next (&it);
    BTOR_PUSH_STACK (stack, cur);
    while (!BTOR_EMPTY_STACK (stack))
    {
      cur = BTOR_REAL_ADDR_NODE (BTOR_POP_STACK (stack));
      if (btor_hashint_table_contains (mark, cur->id)) continue;
      btor_hashint_table_add (mark, cur->id);
      for (i = 0; i < cur->arity; i++)
      {
        e = BTOR_REAL_ADDR_NODE (cur->e[i]);
        if (!cur->parameterized && btor_is_and_node (cur)
            && !btor_hashptr_table_get (slv->score, e))
        {
          btor_hashptr_table_add (slv->score, btor_copy_exp (btor, e));
          /* push onto working stack */
          BTOR_PUSH_STACK (nodes, e);
        }
        BTOR_PUSH_STACK (stack, e);
      }
    }
  }

  BTOR_RELEASE_STACK (stack);
  btor_hashint_table_delete (mark);

  compute_scores_aux (btor, &nodes);

  BTOR_RELEASE_STACK (nodes);

  slv->time.search_init_apps_compute_scores += btor_util_time_stamp () - start;
}

void
btor_compute_scores_dual_prop (Btor *btor)
{
  assert (btor);

  int i;
  double start;
  BtorFunSolver *slv;
  BtorNode *cur;
  BtorNodePtrStack stack, nodes;
  BtorPtrHashTableIterator it;
  BtorIntHashTable *mark;
  BtorMemMgr *mm;

  /* computing scores only required for BTOR_JUST_HEUR_BRANCH_MIN_DEP and
   * BTOR_JUST_HEUR_BRANCH_MIN_APP */
  if (btor_opt_get (btor, BTOR_OPT_FUN_JUST_HEURISTIC) == BTOR_JUST_HEUR_LEFT)
    return;

  start = btor_util_time_stamp ();
  mm    = btor->mm;
  BTOR_INIT_STACK (mm, stack);
  mark = btor_hashint_table_new (mm);

  slv = BTOR_FUN_SOLVER (btor);

  /* Collect all nodes we actually need the score for.  If just is enabled, we
   * only need the children of AND nodes. If dual prop is enabled, we only need
   * APPLY nodes (BV var nodes always have score 0 or 1 depending on the
   * selected heuristic and are treated as such in compare_scores).
   * -> see btor_compute_scores */

  BTOR_INIT_STACK (mm, nodes);

  if (!slv->score)
    slv->score = btor_hashptr_table_new (mm,
                                         (BtorHashPtr) btor_hash_exp_by_id,
                                         (BtorCmpPtr) btor_compare_exp_by_id);

  /* collect applies in bv skeleton */
  btor_iter_hashptr_init (&it, btor->synthesized_constraints);
  btor_iter_hashptr_queue (&it, btor->assumptions);
  while (btor_iter_hashptr_has_next (&it))
  {
    cur = btor_iter_hashptr_next (&it);
    BTOR_PUSH_STACK (stack, cur);
    while (!BTOR_EMPTY_STACK (stack))
    {
      cur = BTOR_REAL_ADDR_NODE (BTOR_POP_STACK (stack));
      if (btor_hashint_table_contains (mark, cur->id)) continue;
      btor_hashint_table_add (mark, cur->id);

      if (btor_is_apply_node (cur) || btor_is_fun_eq_node (cur))
      {
        assert (!cur->parameterized);
        if (!btor_hashptr_table_get (slv->score, cur))
        {
          btor_hashptr_table_add (slv->score, btor_copy_exp (btor, cur));
          /* push onto working stack */
          BTOR_PUSH_STACK (nodes, cur);
        }
        continue;
      }

      for (i = 0; i < cur->arity; i++) BTOR_PUSH_STACK (stack, cur->e[i]);
    }
  }

  BTOR_RELEASE_STACK (stack);
  btor_hashint_table_delete (mark);

  /* compute scores from applies downwards */
  compute_scores_aux (btor, &nodes);

  BTOR_RELEASE_STACK (nodes);

  slv->time.search_init_apps_compute_scores += btor_util_time_stamp () - start;
}

int
btor_compare_scores (Btor *btor, BtorNode *a, BtorNode *b)
{
  assert (btor);
  assert (a);
  assert (b);

  int h, sa, sb;
  BtorFunSolver *slv;
  BtorPtrHashBucket *bucket;

  slv = BTOR_FUN_SOLVER (btor);

  h  = btor_opt_get (btor, BTOR_OPT_FUN_JUST_HEURISTIC);
  a  = BTOR_REAL_ADDR_NODE (a);
  b  = BTOR_REAL_ADDR_NODE (b);
  sa = sb = 0;

  if (!slv->score) return 0;

  if (h == BTOR_JUST_HEUR_BRANCH_MIN_APP)
  {
    if (btor_is_bv_var_node (a))
      sa = 0;
    else
    {
      bucket = btor_hashptr_table_get (slv->score, a);
      assert (bucket);
      sa = ((BtorPtrHashTable *) bucket->data.as_ptr)->count;
    }

    if (btor_is_bv_var_node (b))
      sb = 0;
    else
    {
      bucket = btor_hashptr_table_get (slv->score, b);
      assert (bucket);
      sb = ((BtorPtrHashTable *) bucket->data.as_ptr)->count;
    }
  }
  else if (h == BTOR_JUST_HEUR_BRANCH_MIN_DEP)
  {
    bucket = btor_hashptr_table_get (slv->score, a);
    assert (bucket);
    sa = bucket->data.as_int;

    bucket = btor_hashptr_table_get (slv->score, b);
    assert (bucket);
    sb = bucket->data.as_int;
  }

  return sa < sb;
}

int
btor_compare_scores_qsort (const void *p1, const void *p2)
{
  int h, sa, sb;
  BtorFunSolver *slv;
  Btor *btor;
  BtorNode *a, *b;
  BtorPtrHashBucket *bucket;

  sa = sb = 0;
  a       = *((BtorNode **) p1);
  b       = *((BtorNode **) p2);
  assert (a->btor == b->btor);
  btor = a->btor;
  slv  = BTOR_FUN_SOLVER (btor);

  h = btor_opt_get (btor, BTOR_OPT_FUN_JUST_HEURISTIC);

  if (!slv->score) return 0;

  if (h == BTOR_JUST_HEUR_BRANCH_MIN_APP)
  {
    if (btor_is_bv_var_node (a))
      sa = 0;
    else
    {
      bucket = btor_hashptr_table_get (slv->score, a);
      assert (bucket);
      assert (bucket->data.as_ptr);
      sa = ((BtorPtrHashTable *) bucket->data.as_ptr)->count;
    }

    if (btor_is_bv_var_node (b))
      sb = 0;
    else
    {
      bucket = btor_hashptr_table_get (slv->score, b);
      assert (bucket);
      assert (bucket->data.as_ptr);
      sb = ((BtorPtrHashTable *) bucket->data.as_ptr)->count;
    }
  }
  else if (h == BTOR_JUST_HEUR_BRANCH_MIN_DEP)
  {
    if (btor_is_bv_var_node (a))
      sa = 1;
    else
    {
      bucket = btor_hashptr_table_get (slv->score, a);
      assert (bucket);
      sa = bucket->data.as_int;
    }

    if (btor_is_bv_var_node (b))
      sb = 1;
    else
    {
      bucket = btor_hashptr_table_get (slv->score, b);
      assert (bucket);
      sb = bucket->data.as_int;
    }
  }

  if (sa < sb) return 1;
  if (sa > sb) return -1;
  return 0;
}
