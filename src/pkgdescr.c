#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <pkg.h>

static void free_pkg_descr_entry( pkg_descr_entry *, int );
static void free_pkg_descr_hdr( pkg_descr_hdr *, int );
static int grow_num_entries_expansion( int );
static int grow_num_entries( pkg_descr * );
static int parse_directory_entry( pkg_descr_entry *, char ** );
static int parse_entry_from_line( pkg_descr_entry *, char * );
static int parse_file_entry( pkg_descr_entry *, char ** );
static int parse_hash( char *, unsigned char * );
static int parse_header_from_line( pkg_descr_hdr *, char * );
static int parse_symlink_entry( pkg_descr_entry *, char ** );
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

static int grow_num_entries_expansion( int entries ) {
  int new_entries;

  if ( entries >= 0 ) {
    if ( entries > 0 ) {
      new_entries = entries + ( entries >> 2 );
      if ( new_entries <= entries ) new_entries = entries + 1;
      return new_entries;
    }
    else return 1;
  }
  else return entries;
}

static int grow_num_entries( pkg_descr *descr ) {
  int status, new_num_alloced;
  pkg_descr_entry *new_entries;

  status = 0;
  if ( descr ) {
    new_num_alloced =
      grow_num_entries_expansion( descr->num_entries_alloced );
    if ( new_num_alloced > descr->num_entries_alloced ) {
      if ( descr->num_entries_alloced > 0 ) {
	if ( descr->entries ) {
	  new_entries = realloc( descr->entries,
				 sizeof( *(descr->entries) ) *
				 new_num_alloced );
	}
	else {
	  fprintf( stderr, "Bad pkg_descr %p seen in grow_num_entries()\n",
		   descr );
	  new_entries = NULL;
	  status = -1;
	}
      }
      else {
	new_entries = malloc( sizeof( *(descr->entries) ) * new_num_alloced );
	if ( !new_entries ) status = -1;
      }
      if ( new_entries ) {
	descr->entries = new_entries;
	descr->num_entries_alloced = new_num_alloced;
      }
      else {
	fprintf( stderr,
		 "Couldn't allocate new entries in grow_num_entries()\n" );
      }
    }
    else status = -1;
  }
  else status = -1;
  return status;
}

static int parse_directory_entry( pkg_descr_entry *e, char **fields ) {
  int status, result;
  char *filename, *owner, *group;
  unsigned int mode;

  status = 0;
  if ( e && fields ) {
    if ( strlistlen( fields ) == 4 ) {
      if ( strlen( fields[0] ) > 0 &&
	   strlen( fields[1] ) > 0 &&
	   strlen( fields[2] ) > 0 &&
	   strlen( fields[3] ) > 0 ) {
	filename = copy_string( fields[0] );
	owner = copy_string( fields[1] );
	group = copy_string( fields[2] );
	if ( filename && owner && group ) {
	  result = sscanf( fields[3], "%o", &mode );
	  if ( result == 1 ) {
	    e->filename = filename;
	    e->owner = owner;
	    e->group = group;
	    e->u.d.mode = (mode_t)mode;
	  }
	  else {
	    if ( filename ) free( filename );
	    if ( owner ) free( owner );
	    if ( group ) free( group );	
	    fprintf( stderr,
		     "Error parsing mode %s while parsing directory entry.\n",
		     fields[3] );
	    status = -1;
	  }
	}
	else {
	  if ( filename ) free( filename );
	  if ( owner ) free( owner );
	  if ( group ) free( group );
	  fprintf( stderr,
		   "Failed to allocate memory parsing directory entry.\n" );
	  status = -1;
	}
      }
      else {
	fprintf( stderr, "A field was empty in directory entry\n" );
	status = -1;
      }
    }
    else {
      fprintf( stderr, "Wrong number of fields parsing directory entry\n" );
      status = -1;
    }
  }
  else status = -1;
  return status;
}

static int parse_entry_from_line( pkg_descr_entry *e, char *line ) {
  int status, result;
  char **fields;

  status = 0;
  if ( e && line ) {
    result = parse_strings_from_line( line, &fields );
    if ( result == 0 ) {
      if ( strlistlen( fields ) >= 1 ) {
	if ( strcmp( fields[0], "f" ) == 0 ) {
	  /* it's a file entry */
	  e->type = ENTRY_FILE;
	  status = parse_file_entry( e, fields + 1 );
	}
	else if ( strcmp( fields[0], "d" ) == 0 ) {
	  /* it's a directory entry */
	  e->type = ENTRY_DIRECTORY;
	  status = parse_directory_entry( e, fields + 1 );
	}
	else if ( strcmp( fields[0], "s" ) == 0 ) {
	  /* it's a symlink entry */
	  e->type = ENTRY_SYMLINK;
	  status = parse_symlink_entry( e, fields + 1 );
	}
	else {
	  fprintf( stderr, "Syntax error parsing pkg_descr entry: " );
	  fprintf( stderr, "unknown entry type \"%s\"\n", fields[0] );
	  status = -1;
	}
      }
      else {
	fprintf( stderr, "Syntax error parsing pkg_descr entry: " );
	fprintf( stderr, "too few fields for any entry type\n" );
	status = -1;
      }
      free( fields );
    }
    else status = result;
  }
  else status = -1;
  return status;
}

static int parse_file_entry( pkg_descr_entry *e, char **fields ) {
  int status, result;
  char *filename, *owner, *group;
  unsigned int mode;
  unsigned char hash[HASH_LEN];

  status = 0;
  if ( e && fields ) {
    if ( strlistlen( fields ) == 5 ) {
      if ( strlen( fields[0] ) > 0 &&
	   strlen( fields[1] ) > 0 &&
	   strlen( fields[2] ) > 0 &&
	   strlen( fields[3] ) > 0 &&
	   strlen( fields[4] ) > 0 ) {
	filename = copy_string( fields[0] );
	owner = copy_string( fields[2] );
	group = copy_string( fields[3] );
	if ( filename && owner && group ) {
	  result = parse_hash( fields[1], hash );
	  if ( result == 0 ) {
	    result = sscanf( fields[4], "%o", &mode );
	    if ( result == 1 ) {
	      e->filename = filename;
	      e->owner = owner;
	      e->group = group;
	      e->u.f.mode = (mode_t)mode;
	      memcpy( &(e->u.f.hash), hash, sizeof( hash ) );
	    }
	    else {
	      if ( filename ) free( filename );
	      if ( owner ) free( owner );
	      if ( group ) free( group );	
	      fprintf( stderr,
		       "Error parsing mode %s while parsing file entry.\n",
		       fields[4] );
	      status = -1;
	    }
	  }
	  else {
	      if ( filename ) free( filename );
	      if ( owner ) free( owner );
	      if ( group ) free( group );	
	      fprintf( stderr,
		       "Error parsing hash while parsing file entry.\n" );
	      status = -1;
	  }
	}
	else {
	  if ( filename ) free( filename );
	  if ( owner ) free( owner );
	  if ( group ) free( group );
	  fprintf( stderr,
		   "Failed to allocate memory parsing file entry.\n" );
	  status = -1;
	}
      }
      else {
	fprintf( stderr, "A field was empty in file entry\n" );
	status = -1;
      }
    }
    else {
      fprintf( stderr, "Wrong number of fields parsing file entry\n" );
      status = -1;
    }
  }
  else status = -1;
  return status;
}

static int parse_hash( char *s, unsigned char *hash_out ) {
  int status, result, i;
  unsigned char temp[2], hash_temp[HASH_LEN];
  unsigned int digit_h, digit_l, hash_byte;

  status = 0;
  if ( s && hash_out ) {
    if ( strlen( s ) == 2 * HASH_LEN ) {
      temp[1] = '\0';
      for ( i = 0; i < HASH_LEN; ++i ) {
	temp[0] = s[2*i];
	result = sscanf( temp, "%x", &digit_h );
	if ( result != 1 ) {
	  status = -1;
	  break;
	}
	if ( digit_h > 0xf ) {
	  status = -1;
	  break;
	}
	temp[0] = s[2*i+1];
	result = sscanf( temp, "%x", &digit_l );
	if ( result != 1 ) {
	  status = -1;
	  break;
	}
	if ( digit_l > 0xf ) {
	  status = -1;
	  break;
	}
	hash_byte = ( ( digit_h & 0xf ) << 4 ) | ( digit_l & 0xf );
	hash_temp[i] = (unsigned char)hash_byte;
      }
      if ( status == 0 ) memcpy( hash_out, hash_temp, sizeof( hash_temp ) );
    }
    else {
      status = -1;
    }
  }
  else status = -1;
  return status;
}

static int parse_header_from_line( pkg_descr_hdr *h, char *line ) {
  int status, result, pkg_name_len;
  char **fields;
  char *pkg_name, *pkg_time_str, *pkg_root, *temp;
  unsigned long pkg_time;

  status = 0;
  if ( h && line ) {
    result = parse_strings_from_line( line, &fields );
    if ( result == 0 ) {
      if ( strlistlen( fields ) == 3 ) {
	pkg_name = fields[0];
	pkg_time_str = fields[1];
	pkg_root = fields[2];
	pkg_name_len = strlen( pkg_name );
	if ( pkg_name_len > 0 ) {
	  if ( strcmp( pkg_root, "/" ) == 0 ) {
	    result = sscanf( pkg_time_str, "%lu", &pkg_time );
	    if ( result == 1 ) {
	      temp = malloc( sizeof( char ) * ( pkg_name_len + 1 ) );
	      if ( temp ) {
		strncpy( temp, pkg_name, pkg_name_len + 1 );
		h->pkg_name = temp;
		h->pkg_time = (time_t)pkg_time;
	      }
	      else {
		fprintf( stderr, "Couldn't allocate memory in parse_header_from_line()\n" );
		status = -1;
	      }
	    }
	    else {
	      fprintf( stderr, "Syntax error parsing pkg_descr header: " );
	      fprintf( stderr, "couldn't parse pkg_time \"%s\"\n",
		       pkg_time_str );
	      status = -1;	      
	    }
	  }
	  else {
	    /*
	     * pkg_root was a feature in the old perl version that
	     * went unused, so it isn't supported and must be set to
	     * "/" in this C re-implementation.
	     */
	    fprintf( stderr, "Syntax error parsing pkg_descr header: " );
	    fprintf( stderr, "pkg_root wasn't \"/\"\n" );
	    status = -1;
	  }
	}
	else {
	  fprintf( stderr, "Syntax error parsing pkg_descr header: " );
	  fprintf( stderr, "pkg_name was empty\n" );
	  status = -1;
	}
      }
      else {
	fprintf( stderr, "Syntax error parsing pkg_descr header: " );
	fprintf( stderr, "wrong number of fields\n" );
	status = -1;
      }
      free( fields );
    }
    else status = result;
  }
  else status = -1;
  return status;
}

static int parse_symlink_entry( pkg_descr_entry *e, char **fields ) {
  int status;
  char *filename, *target, *owner, *group;

  status = 0;
  if ( e && fields ) {
    if ( strlistlen( fields ) == 4 ) {
      if ( strlen( fields[0] ) > 0 &&
	   strlen( fields[1] ) > 0 &&
	   strlen( fields[2] ) > 0 &&
	   strlen( fields[3] ) > 0 ) {
	filename = copy_string( fields[0] );
	target = copy_string( fields[1] );
	owner = copy_string( fields[2] );
	group = copy_string( fields[3] );
	if ( filename && target && owner && group ) {
	  e->filename = filename;
	  e->owner = owner;
	  e->group = group;
	  e->u.s.target = target;
	}
	else {
	  if ( filename ) free( filename );
	  if ( target ) free( target );
	  if ( owner ) free( owner );
	  if ( group ) free( group );
	  fprintf( stderr,
		   "Failed to allocate memory parsing symlink entry.\n" );
	  status = -1;
	}
      }
      else {
	fprintf( stderr, "A field was empty in symlink entry\n" );
	status = -1;
      }
    }
    else {
      fprintf( stderr, "Wrong number of fields parsing symlink entry\n" );
      status = -1;
    }
  }
  else status = -1;
  return status;
}

pkg_descr * read_pkg_descr_from_file( char *filename ) {
  FILE *fp;
  pkg_descr *descr;
  int seen_header, result, status;
  char *line;

  if ( filename ) {
    descr = malloc( sizeof( *descr ) );
    if ( descr ) {
      descr->hdr.pkg_name = NULL;
      descr->num_entries = 0;
      descr->num_entries_alloced = 0;
      descr->entries = NULL;
      fp = fopen( filename, "r" );
      if ( fp ) {
	seen_header = 0;
	status = 0;
	while ( line = read_line_from_file( fp ) ) {
	  if ( !is_whitespace( line ) ) {
	    if ( !seen_header ) {
	      status = parse_header_from_line( &(descr->hdr), line );
	      if ( status != 0 ) {
		fprintf( stderr, "Error trying to parse pkg_descr header\n" );
	      }
	      seen_header = 1;
	    }
	    else {
	      while ( !( descr->num_entries < descr->num_entries_alloced ) ) {
		status = grow_num_entries( descr );
		if ( status != 0 ) {
		  fprintf( stderr, "Couldn't allocate more entries while reading pkg_descr\n" );
		  break;
		}
	      }
	      if ( status == 0 ) {
		status =
		  parse_entry_from_line( &(descr->entries[descr->num_entries]),
					 line );
		if ( status == 0 ) ++(descr->num_entries);
		else {
		  fprintf( stderr,
			   "Error trying to parse pkg_descr entry %d\n",
			   descr->num_entries );
		}
	      }
	    }
	  }
	  free( line );
	  if ( status != 0 ) break;
	}
	fclose( fp );
	if ( status == 0 ) {
	  return descr;
	}
	else {
	  free_pkg_descr( descr );
	  return NULL;
	}
      }
      else {
	fprintf( stderr, "Couldn't open %s to read pkg_descr\n", filename );
	return NULL;
      }
    }
    else {
      fprintf( stderr, "Couldn't allocate memory while reading pkg_descr\n" );
      return NULL;
    }
  }
  else return NULL;
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
    else {
      fprintf( stderr, "Couldn't open %s to write pkg_descr\n", filename );
      status = -1;
    }
  }
  else status = -1;
  return status;
}
