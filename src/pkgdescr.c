#include <stdlib.h>
#include <stdio.h>

#include <pkg.h>

static void free_pkg_descr_entry( pkg_descr_entry *, int );
static void free_pkg_descr_hdr( pkg_descr_hdr *, int );
static int write_pkg_descr_entry( FILE *, pkg_descr_entry * );
static int write_pkg_descr_hdr( FILE *, pkg_descr_hdr * );

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
    default:
      fprintf( stderr, "Warning: free_pkg_descr_entry( %p, %d ) saw unknown entry type %d.\n",
	       p, free_entry_struct, p->type );
    }
    if ( free_entry_struct ) free( p );
  }
}

static void free_pkg_descr_hdr( pkg_descr_hdr *p,
				int free_hdr_struct ) {
  if ( p ) {
    if ( p->pkg_name ) free( p->pkg_name );
    if ( free_hdr_struct ) free( p );
  }
}

void free_pkg_descr( pkg_descr *p ) {
  int i;

  if ( p ) {
    free_pkg_descr_hdr( &(p->hdr), 0 );
    if ( p->entries ) {
      for ( i = 0; i < p->num_entries; ++i )
	free_pkg_descr_entry( &(p->entries[i]), 0 );
      free( p->entries );
    }
    free( p );
  }
}

static int write_pkg_descr_entry( FILE *fp, pkg_descr_entry *entry ) {
  int status, result;
  char *str_temp;

  status = 0;
  if ( fp && entry ) {
    switch ( entry->type ) {
    case ENTRY_FILE:
      str_temp = hash_to_string( entry->u.f.hash, HASH_LEN );
      if ( str_temp ) {
	result = fprintf( fp, "f %s %s %s %s %04o\n",
			  entry->filename, str_temp,
			  entry->owner, entry->group,
			  entry->u.f.mode );
	if ( result < 0 ) {
	  fprintf( stderr, "Error writing pkg_descr_entry %p\n", entry );
	  status = result;
	}
	free( str_temp );
      }
      else {
	fprintf( stderr,
		 "Error allocating memory writing pkg_descr_entry %p\n",
		 entry );
	status = -1;
      }
      break;
    case ENTRY_DIRECTORY:
      result = fprintf( fp, "d %s %s %s %04o\n", entry->filename,
			entry->owner, entry->group,
			entry->u.d.mode );
      if ( result < 0 ) {
	fprintf( stderr, "Error writing pkg_descr_entry %p\n", entry );
	status = result;
      }
      break;
    case ENTRY_SYMLINK:
      result = fprintf( fp, "s %s %s %s %s\n", entry->filename,
			entry->u.s.target, entry->owner,
			entry->group );
      if ( result < 0 ) {
	fprintf( stderr, "Error writing pkg_descr_entry %p\n", entry );
	status = result;
      }
      break;
    default:
      fprintf( stderr, "Unknown type %d writing pkg_descr_entry %p\n",
	       entry->type, entry );
      status = -1;
    }
  }
  else status = -1;
  return status;
}

static int write_pkg_descr_hdr( FILE *fp, pkg_descr_hdr *hdr ) {
  int status, result;

  status = 0;
  if ( fp && hdr ) {
    result = fprintf( fp, "%s %lu /\n",
		      hdr->pkg_name, (unsigned long)(hdr->pkg_time) );
    if ( result < 0 ) {
      fprintf( stderr, "Error writing pkg_descr_hdr %p\n", hdr );
      status = result;
    }
  }
  else status = -1;
  return status;
}

int write_pkg_descr_to_file( pkg_descr *descr, char *filename ) {
  FILE *fp;
  int status, result, i;

  status = 0;
  if ( descr && filename ) {
    fp = fopen( filename, "w" );
    if ( fp ) {
      result = write_pkg_descr_hdr( fp, &(descr->hdr) );
      if ( result == 0 ) {
	for ( i = 0; i < descr->num_entries; ++i ) {
	  result = write_pkg_descr_entry( fp, &(descr->entries[i]) );
	  if ( result != 0 ) {
	    status = result;
	    break;
	  }
	}
      }
      else status = result;
      fclose( fp );
    }
    else status = -1;
  }
  else status = -1;
  return status;
}
