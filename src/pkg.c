#include <stdlib.h>
#include <stdio.h>

#include <pkg.h>

int main( int, char **, char ** );

int main( int argc, char **argv, char **envp ) {
  pkg_handle *p;

  init_pkg_globals();
  if ( argc == 2 ) {
    printf( "Trying to open \"%s\".\n", argv[1] );
    p = open_pkg_file( argv[1] );
    if ( p ) {
      printf( "Got it, in \"%s\".\n", p->unpacked_dir );
      close_pkg( p );
    }
    else printf( "Failed\n" );
  }
  else {
    fprintf( stderr, "Need 2 args\n" );
  }

  return 0;
}
