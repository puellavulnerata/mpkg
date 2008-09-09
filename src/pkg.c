#include <stdlib.h>
#include <stdio.h>

#include <pkg.h>

int main( int, char **, char ** );

int main( int argc, char **argv, char **envp ) {
  int status;

  if ( argc == 2 ) {
    status = recrm( argv[1] );
    if ( status != 0 ) {
      fprintf( stderr, "%s\n", strerror( status ) );
    }
  }
  else {
    fprintf( stderr, "Must have 1 arg\n" );
  }

  return 0;
}
