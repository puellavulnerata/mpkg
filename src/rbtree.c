#include <stdlib.h>
#include <string.h>

#include <pkg.h>

int rbtree_string_comparator( void *x, void *y ) {
  char *sx, *sy;
  int result;

  if ( x && y ) {
    sx = (char *)x;
    sy = (char *)y;
    result = strcmp( sx, sy );
    if ( result < 0 ) return -1;
    else if ( result == 0 ) return 0;
    else return 1;
  }
  else {
    if ( !x && y ) return 1;
    else if ( x && !y ) return -1;
    else return 0;
  }
}
