#include <stdlib.h>
#include <stdio.h>

#include <pkg.h>

static void free_pkg_descr_entry( pkg_descr_entry *, int );
static void free_pkg_descr_header( pkg_descr_hdr *, int );

static void free_pkg_descr_entry( pkg_descr_entry *p,
				  int free_entry_struct ) {
  if ( p ) {
    if ( p->filename ) free( p->filename );
    if ( p->owner ) free( p->owner );
    if ( p->group ) free( p->group );
    switch ( p->type ) {
    case ENTRY_FILE:
      /* Nothing further to free for file entries */
      break;
    case ENTRY_DIRECTORY:
      /* Nothing further to free for directory entries */
      break;
    case ENTRY_SYMLINK:
      if ( p->u.s.target )
	free( p->u.s.target );
      break;
      /* default: */
      /* Do nothing for the default case */
      /* TODO - Maybe print a warning about an unknown entry type? */
    }
    if ( free_entry_struct ) free( p );
  }
}

static void free_pkg_descr_header( pkg_descr_hdr *p,
				   int free_hdr_struct ) {
  if ( p ) {
    if ( p->pkg_name ) free( p->pkg_name );
    if ( free_hdr_struct ) free( p );
  }
}

void free_pkg_descr( pkg_descr *p ) {
  int i;

  if ( p ) {
    free_pkg_descr_header( &(p->hdr), 0 );
    if ( p->entries ) {
      for ( i = 0; i < p->num_entries; ++i )
	free_pkg_descr_entry( &(p->entries[i]), 0 );
      free( p->entries );
    }
    free( p );
  }
}
