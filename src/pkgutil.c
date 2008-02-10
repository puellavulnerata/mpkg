#include <stdlib.h>

#include <pkg.h>

static char hex_digit_to_char( unsigned char );

char * hash_to_string( unsigned char *hash, unsigned long len ) {
  char *str_temp;
  unsigned long i;

  if ( hash && len > 0 ) {
    str_temp = malloc( 2 * len + 1 );
    if ( str_temp ) {
      for ( i = 0; i < len; ++i ) {
	str_temp[2*i] = hex_digit_to_char( ( hash[i] >> 4 ) & 0xf );
	str_temp[2*i+1] = hex_digit_to_char( hash[i] & 0xf );
      }
      str_temp[2*len] = '\0';
      return str_temp;
    }
    else return NULL;
  }
  else return NULL;
}

static char hex_digit_to_char( unsigned char x ) {
  switch ( x ) {
  case 0x0:
    return '0';
    break;
  case 0x1:
    return '1';
    break;
  case 0x2:
    return '2';
    break;
  case 0x3:
    return '3';
    break;
  case 0x4:
    return '4';
    break;
  case 0x5:
    return '5';
    break;
  case 0x6:
    return '6';
    break;
  case 0x7:
    return '7';
    break;
  case 0x8:
    return '8';
    break;
  case 0x9:
    return '9';
    break;
  case 0xa:
    return 'a';
    break;
  case 0xb:
    return 'b';
    break;
  case 0xc:
    return 'c';
    break;
  case 0xd:
    return 'd';
    break;
  case 0xe:
    return 'e';
    break;
  case 0xf:
    return 'f';
    break;
  default:
    return -1;
  }
}
