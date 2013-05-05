#include <stdio.h>
#include <stdlib.h>
#include "index.h"

int main(int argc, char * argv[])
{
  index_search_results_t * results;
  init_index();
  insert_into_index("hello", "test.c", 10);
  insert_into_index("hello", "test.c", 20);
  insert_into_index("hello", "foo.c", 30);
  insert_into_index("goodbye", "test.c", 10);


  results = find_in_index("hello");
  if (results) {
    int i;
    for (i = 0; i < results->num_results; i++) {
      printf("%s: %d\n",
	     results->results[i].file_name,
	     results->results[i].line_number);
    }
    free(results);
  }
  find_in_index("goodbye");
  return(0);
}
