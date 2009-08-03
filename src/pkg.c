#include <pkg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_MTRACE
#include <mcheck.h>
#endif

static void help_callback( int, char ** );
static void help_help( void );
int main( int, char **, char ** );
static void version_callback( int, char ** );
static void version_help( void );

struct cmd_s {
  char *name;
  void (*callback)( int, char ** );
  void (*help)( void );
} cmd_table[] = {
  { "convert", convert_main, convert_help },
  { "convertdb", convertdb_main, convertdb_help },
  { "create", create_main, create_help },
  { "createdb", createdb_main, createdb_help },
  { "dumpdb", dumpdb_main, dumpdb_help },
  { "help", help_callback, help_help },
  { "install", install_main, install_help },
  { "remove", remove_main, remove_help },
  { "repairdb", repairdb_main, repairdb_help },
  { "status", status_main, status_help },
  { "version", version_callback, version_help },
  { NULL, NULL, NULL }
};

static void help_callback( int argc, char **argv ) {
  int i, found_it;

  if ( argc == 0 ) {
    printf( "Usage: mpkg [global options] command [command options]\n" );
    printf( "\n" );
    printf( "The global options are:\n" );
    printf( "\t--enable-md5:\tEnable MD5 checking\n" );
    printf( "\t--disable-md5:" );
    printf( "\tDisable MD5 checking (use mtimes instead)\n" );
    printf( "\n" );
    printf( "\t--instroot <path>:\tUse <path> as root for packages\n" );
    printf( "\t--pkgdir <path>:\tUse package database and descriptions " );
    printf( "in <path>\n" );
    printf( "\t--tempdir <path>:\tKeep temp files in <path>\n" );
    printf( "\n" );
    printf( "The commands are:\n\n" );

    i = 0;
    while ( cmd_table[i].name != NULL ) {
      printf( "\t%s\n", cmd_table[i++].name );
    }
    printf( "\n" );
    printf( "Use mpkg help <command> for command-specific help.\n" );
  }
  else if ( argc == 1 ) {
    i = 0;
    found_it = 0;
    while ( cmd_table[i].name != NULL && !found_it ) {
      if ( strcmp( cmd_table[i].name, argv[0] ) == 0 ) {
	found_it = 1;
	if ( cmd_table[i].help ) cmd_table[i].help();
	else {
	  printf( "No help available for command '%s'.\n", argv[0] );
	}
      }
      ++i;
    }

    if ( !found_it ) {
      fprintf( stderr, "Unknown command %s.  Try 'mpkg help'.\n",
	       argv[0] );
    }
  }
  else {
    printf( "Wrong number of parameters; try 'mpkg help help'\n" );
  }
}

static void help_help( void ) {
  printf( "This is the mpkg help command.  It gives usage information" );
  printf( " on specified mpkg commands.  Usage:\n" );
  printf( "\n" );
  printf( "mpkg [global options] help [<command>]\n" );
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
	  if ( cmd_table[i].callback ) {
	    cmd_table[i].callback( cmd_argc, cmd_argv );
	    error = 0;
	  }
	  else {
	    fprintf( stderr, "Command %s not implemented\n",
		     cmd_table[i].name );
	  }
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

static void version_callback( int argc, char **argv ) {
#ifdef BUILD_DATE
  printf( "mpkg %s, built on %s\n", VERSION, BUILD_DATE );
#else /* !defined(BUILD_DATE) */
  printf( "mpkg %s\n", VERSION );
#endif /* BUILD_DATE */
  printf( "Written by Andrea Shepard, with Berkeley DB code by Dana Koch.\n" );
  printf( "\n" );
  printf( "Released into the public domain; " );
  printf( "copyright is just plain ridiculous.\n" );
}

static void version_help( void ) {
  printf( "Display version information.  Usage:\n" );
  printf( "\n" );
  printf( "mpkg [global options] version\n" );
}
