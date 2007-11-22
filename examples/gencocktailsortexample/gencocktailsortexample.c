#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../boolector.h"
#include "../../btorutil.h"

static int
num_chars (int x)
{
  int result = 0;
  if (x == 0) return 1;
  while (x > 0)
  {
    result++;
    x /= 10;
  }
  return result;
}

int
main (int argc, char **argv)
{
  int num_bits, num_bits_index, num_elements, i, top, bottom;
  char *buffer;
  Btor *btor;
  BtorExp **indices, **initial_elements, **sorted_elements, *array, *eq, *ugt,
      *ult, *ulte;
  BtorExp *temp, *read1, *read2, *cond1, *cond2, *sorted, *formula, *var;
  bottom = 0;
  if (argc != 3)
  {
    printf ("Usage: ./gencocktailsortexample <num-bits> <num-elements>\n");
    return 1;
  }
  num_bits = atoi (argv[1]);
  if (num_bits <= 0)
  {
    printf ("Number of bits must be greater than zero\n");
    return 1;
  }
  num_elements = atoi (argv[2]);
  if (num_elements <= 1)
  {
    printf ("Number of elements must be greater than one\n");
    return 1;
  }
  if (!btor_is_power_of_2_util (num_elements))
  {
    printf ("Number of elements must be a power of two\n");
    return 1;
  }
  num_bits_index   = btor_log_2_util (num_elements);
  btor             = btor_new_btor ();
  indices          = (BtorExp **) malloc (sizeof (BtorExp *) * num_elements);
  initial_elements = (BtorExp **) malloc (sizeof (BtorExp *) * num_elements);
  sorted_elements  = (BtorExp **) malloc (sizeof (BtorExp *) * num_elements);
  for (i = 0; i < num_elements; i++)
    indices[i] = btor_int_to_exp (btor, i, num_bits_index);
  array = btor_array_exp (btor, num_bits, num_bits_index);
  /* read initial elements */
  for (i = 0; i < num_elements; i++)
    initial_elements[i] = btor_read_exp (btor, array, indices[i]);
  /* cocktail sort algorithm */
  top = num_elements - 1;
  while (bottom < top)
  {
    for (i = bottom; i < top; i++)
    {
      read1 = btor_read_exp (btor, array, indices[i]);
      read2 = btor_read_exp (btor, array, indices[i + 1]);
      ugt   = btor_ugt_exp (btor, read1, read2);
      /* swap ? */
      cond1 = btor_cond_exp (btor, ugt, read2, read1);
      cond2 = btor_cond_exp (btor, ugt, read1, read2);
      temp  = btor_write_exp (btor, array, indices[i], cond1);
      btor_release_exp (btor, array);
      array = temp;
      temp  = btor_write_exp (btor, array, indices[i + 1], cond2);
      btor_release_exp (btor, array);
      array = temp;
      btor_release_exp (btor, read1);
      btor_release_exp (btor, read2);
      btor_release_exp (btor, ugt);
      btor_release_exp (btor, cond1);
      btor_release_exp (btor, cond2);
    }
    top--;
    for (i = top; i > bottom; i--)
    {
      read1 = btor_read_exp (btor, array, indices[i]);
      read2 = btor_read_exp (btor, array, indices[i - 1]);
      ult   = btor_ult_exp (btor, read1, read2);
      /* swap ? */
      cond1 = btor_cond_exp (btor, ult, read2, read1);
      cond2 = btor_cond_exp (btor, ult, read1, read2);
      temp  = btor_write_exp (btor, array, indices[i], cond1);
      btor_release_exp (btor, array);
      array = temp;
      temp  = btor_write_exp (btor, array, indices[i - 1], cond2);
      btor_release_exp (btor, array);
      array = temp;
      btor_release_exp (btor, read1);
      btor_release_exp (btor, read2);
      btor_release_exp (btor, ult);
      btor_release_exp (btor, cond1);
      btor_release_exp (btor, cond2);
    }
    bottom++;
  }
  /* read sorted elements */
  for (i = 0; i < num_elements; i++)
    sorted_elements[i] = btor_read_exp (btor, array, indices[i]);
  /* show that array is sorted */
  sorted = btor_const_exp (btor, "1");
  for (i = 0; i < num_elements - 1; i++)
  {
    read1 = btor_read_exp (btor, array, indices[i]);
    read2 = btor_read_exp (btor, array, indices[i + 1]);
    ulte  = btor_ulte_exp (btor, read1, read2);
    temp  = btor_and_exp (btor, sorted, ulte);
    btor_release_exp (btor, sorted);
    sorted = temp;
    btor_release_exp (btor, read1);
    btor_release_exp (btor, read2);
    btor_release_exp (btor, ulte);
  }
  formula = btor_copy_exp (btor, sorted);
  /* we set variables equal to the initial read values */
  for (i = 0; i < num_elements; i++)
  {
    buffer = (char *) malloc (sizeof (char)
                              * (strlen ("initial_v") + num_chars (i) + 1));
    sprintf (buffer, "inital_v%d", i);
    var  = btor_var_exp (btor, num_bits, buffer);
    eq   = btor_eq_exp (btor, var, initial_elements[i]);
    temp = btor_and_exp (btor, formula, eq);
    btor_release_exp (btor, formula);
    formula = temp;
    btor_release_exp (btor, var);
    btor_release_exp (btor, eq);
    free (buffer);
  }
  /* we set variables equal to the sorted read read values */
  for (i = 0; i < num_elements; i++)
  {
    buffer = (char *) malloc (sizeof (char)
                              * (strlen ("sorted_v") + num_chars (i) + 1));
    sprintf (buffer, "sorted_v%d", i);
    var  = btor_var_exp (btor, num_bits, buffer);
    eq   = btor_eq_exp (btor, var, sorted_elements[i]);
    temp = btor_and_exp (btor, formula, eq);
    btor_release_exp (btor, formula);
    formula = temp;
    btor_release_exp (btor, var);
    btor_release_exp (btor, eq);
    free (buffer);
  }
  btor_dump_exp (btor, stdout, formula);
  /* clean up */
  for (i = 0; i < num_elements; i++)
  {
    btor_release_exp (btor, indices[i]);
    btor_release_exp (btor, initial_elements[i]);
    btor_release_exp (btor, sorted_elements[i]);
  }
  btor_release_exp (btor, formula);
  btor_release_exp (btor, sorted);
  btor_release_exp (btor, array);
  btor_delete_btor (btor);
  free (indices);
  free (initial_elements);
  free (sorted_elements);
  return 0;
}
