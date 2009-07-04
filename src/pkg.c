#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef USE_MTRACE
#include <mcheck.h>
#endif

#include <pkg.h>

static void help_callback( int, char ** );
int main( int, char **, char ** );

struct cmd_s {
  char *name;
  void (*callback)( int, char ** );
} cmd_table[] = {
  { "convert", convert_main },
  { "convertdb", convertdb_main },
  { "create", create_main },
  { "createdb", createdb_main },
  { "help", help_callback },
  { "install", install_main },
  { "remove", remove_main },
  { NULL, NULL }
};

static void help_callback( int argc, char **argv ) {
  if ( argc == 1 ) {
    printf( "help %s\n", argv[0] );
  }
  else printf( "help\n" );
}

int main( int argc, char **argv, char **envp ) {
  int i, error;
  char *cmd, *curr;
  int cmd_argc;
  char **cmd_argv;

#ifdef USE_MTRACE
  mtrace();
#endif

  init_pkg_globals();

  i = 1;
  cmd = NULL;
  error = 0;

  while ( i < argc ) {
    curr = argv[i];

    if ( *curr == '-' ) {
      if ( strcmp( curr, "--tempdir" ) == 0 ) {
	if ( i + 1 < argc ) set_temp( argv[++i] );
	else {
	  fprintf( stderr, "--tempdir requires a directory name\n" );
	  error = 1;
	  break;
	}
      }
      else if ( strcmp( curr, "--pkgdir" ) == 0 ) {
	if ( i + 1 < argc ) set_pkg( argv[++i] );
	else {
	  fprintf( stderr, "--pkgdir requires a directory name\n" );
	  error = 2;
	  break;
	}
      }
      else if ( strcmp( curr, "--instroot" ) == 0 ) {
	if ( i + 1 < argc ) set_root( argv[++i] );
	else {
	  fprintf( stderr, "--instroot requires a directory name\n" );
	  error = 3;
	  break;
	}
      }
      else if ( strcmp( curr, "--enable-md5" ) == 0 ) {
	set_check_md5( 1 );
      }
      else if ( strcmp( curr, "--disable-md5" ) == 0 ) {
	set_check_md5( 0 );
      }
      else {
	fprintf( stderr, "Unknown option %s\n", curr );
	error = 4;
	break;
      }
    }
    else {
      cmd = curr;
      cmd_argc = argc - i - 1;
      if ( cmd_argc > 0 ) cmd_argv = argv + i + 1;
      else cmd_argv = NULL;
      break;
    }

    ++i;
  }

  if ( error == 0 ) {
    if ( cmd ) {
      i = 0;

      error = 5;
      while ( cmd_table[i].name != NULL ) {
	if ( strcmp( cmd, cmd_table[i].name ) == 0 ) {
	  cmd_table[i].callback( cmd_argc, cmd_argv );
	  error = 0;
	  break;
	}
	++i;
      }

      if ( error != 0 ) {
	fprintf( stderr, "Unknown command %s.  Try 'mpkg help'.\n",
		 cmd );
      }
    }
    else fprintf( stderr, "A command is required.  Try 'mpkg help'.\n" );
  }

  free_pkg_globals();

#ifdef USE_MTRACE
  muntrace();
#endif

  return 0;
}
