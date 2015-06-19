#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xis.h"
#include "xreloc.h"

/* Tokens: words {.}?[a-zA-Z_][a-zA-Z0-9_*]
           ints  -?[0-9]+
           colon :
           literal "{[^"]|{\"}}*"
           comment #.*$ (ignore)
 */

enum {
  NO_TOKEN,
  LABEL,
  INSTRUCTION,
  DIRECTIVE,
  INT,
  COLON,
  LITERAL,
  REGISTER,
  COMMA,
  BAD_LITERAL,
  BAD_INT,
  BAD_TOKEN
};

enum {
  D_LITERAL,
  D_WORDS,
  D_GLOB,
  D_NUM
};

static char *directives[D_NUM + 1] = { ".literal", ".words", ".glob", NULL };

static int getregister( char *tok,  int *r ) {
  int i;

  if( !r ) {
    r = &i;
  }
  return ( sscanf(tok + 1, "%d", r) == 1 ) && ( *r >= 0 ) && ( *r < XIS_REGS );
}

static int isintdigit( char c, int hex ) {
  c = toupper( c );
  return isdigit( c ) || ( hex && ( c >= 'A' ) && ( c <= 'F' ) );
}

static int token( char *buf, char *tok, int *val ) {
  static char *cur;
  char **k;
  char *w;
  struct x_inst *ip;
  int v;
  char *intp = NULL;
  int hex;

  assert( tok );

  if( buf ) {
    cur = buf;
  }

  if( !val ) {
    val = &v;
  }

  for( ; *cur && isspace( *cur ); cur++ );

  tok[0] = *cur;
  tok[1] = 0;

  switch( *cur ) {
  case 0:
    return NO_TOKEN;
  case '#':
    return NO_TOKEN;
  case '-':
    intp = cur;
    cur++;
    tok++;
    if( !isdigit( *cur ) ) {
      *tok = *cur;
      tok++;
      *tok = 0;
      return BAD_INT;
    }
    break;
  case ':':
    cur++;
    return COLON;
  case ',':
    cur++;
    return COMMA;
  }

  if( *cur == '"' ) {
    for( cur++; *cur && ( *cur != '"' ); cur++ ) {
      if( *cur == '\\' ) {
        cur++;
      }
      *tok = *cur;
      tok++;
    }
    *tok = 0;

    if( *cur ) {
      cur++;
      return LITERAL;
    } else {
      return BAD_LITERAL;
    }
  } else if( isdigit( *cur ) ) {
    hex = ( cur[0] == '0' ) && toupper( cur[1] ) == 'X';
    if( !intp ) {
      intp = cur;
    }

    if( hex ) {
      if( sscanf( intp, "%x", val ) < 1 ) {
        return BAD_INT;
      }
      *tok++ = *cur++;
      *tok++ = *cur++;
    } else {
      sscanf( intp, "%d", val );
    }

    for( ; isintdigit( *cur, hex ); cur++ ) {
      *tok = *cur;
      tok++;
    }

    *tok = 0;
    return INT;
  } else if( isalpha( *cur ) || ( *cur == '_' ) ) {
    w = tok;
    for( ; isdigit( *cur ) || isalpha( *cur ) || ( *cur == '_' ); cur++ ) {
      *tok = *cur;
      tok++;
    }
    *tok = 0;
    if( ( strlen( w ) <= 3 ) && ( *w == 'r' ) && getregister( w, val ) ) {
      return REGISTER;
    }
    for( ip = x_instructions; ip->inst; ip++ ) {
      if( !strcmp( w, ip->inst ) ) {         /* if instruction found */
        return INSTRUCTION;
      }
    }
    return LABEL;
  } else if( *cur == '.' ) {
    w = tok;
    tok++;
    for( cur++; isdigit( *cur ) || isalpha( *cur ) || ( *cur == '_' ); cur++ ) {
      *tok = *cur;
      tok++;
    }
    *tok = 0;
    for( k = directives; *k; k++ ) {
      if( !strcmp( w, *k ) ) {               /* if directive found */
        return DIRECTIVE;
      }
    }
    return BAD_TOKEN;
  } else {
    return BAD_TOKEN;
  }
}


int main( int argc, char **argv ) {

  FILE *fp;
  xreloc relocs;          /* relocations */
  unsigned char *obj;      /* pointer to object code being generated */
  char buf[170];            /* buffers for parsing each line */
  char tok[85];
  int value;               /* current index or address value */
  int r1, r2;              /* current registers to use */
  int pos = 0;             /* current position in object file */
  int size = 0;            /* size of the object file */
  int line = 0;            /* current line being parsed */
  int err = 0;             /* error counter (0 is good */
  int rc = 0;              /* return code */
  struct x_inst *ip;      /* pointer into intruction-pneumonic table */

                           /* check for usage errors */
  if( argc < 3 ) {
    printf( "usage: xas infile.as outfile.xo\n" );
    return 1;
  }

                           /* open input file */
  fp = fopen( argv[1], "r" );
  if( !fp ) {
    printf( "error: could not open input file %s\n", argv[1] );
    return 1;
  }

                           /* allocate memory for object file */
  obj = malloc( XIS_MEM_SIZE + 85 ); /* + 85 for overflow */
  if( !obj ) {
    printf( "error: memory allocation (%d) failed\n", XIS_MEM_SIZE );
    return 1;
  }

  relocs = xreloc_init( obj, stdout );
  if( !relocs ) {
    printf( "error: could not initialize relocation system\n" );
  }

  /* parsing and encoding loop
   */
  while( fgets( buf, 169, fp ) ) {   /* read line from input file */
    line++;

    rc = token( buf, tok, NULL );
    if( rc == LABEL ) {
      if( !xreloc_symbol( relocs, pos, tok ) ) {  /* set label position */
        err++;
      }

      if( token( NULL, tok, NULL ) != COLON ) {   /* if line has a label */
        printf( "error:%d: syntax error, expected a colon after label\n", line);
        err++;
        continue;
      }

      rc = token( NULL, tok, NULL );
    }

    if( rc == NO_TOKEN ) {
      continue;
    } else if( rc == INSTRUCTION ) {
      r1 = 0;
      r2 = 0;
      value = 0;

      // should be hashed
      for( ip = x_instructions; ip->inst; ip++ ) {
        if( !strcmp( tok, ip->inst ) ) {       /* if instruction found */
          if( XIS_NUM_OPS( ip->code ) == 1 ) {
            if( ip->code & XIS_1_IMED ) {
              if( token( NULL, tok, NULL ) != LABEL ) {
                printf( "error:%d: expecting a label operand\n", line );
                err++;
                break;
              }
              /* add addr reloc to be fixed up */
              err += !xreloc_reloc( relocs, pos, XIS_REL_SIZE, tok,
                                    XRELOC_RELATIVE );
            } else if( token( NULL, tok, &r1 ) != REGISTER ) {
              printf( "error:%d: expecting register operand \n", line );
              err++;
              break;
            }
          } else if( XIS_NUM_OPS( ip->code ) == 2 ) {
            if( token( NULL, tok, &r1 ) != REGISTER ) {
              printf( "error:%d: expecting register operand \n", line );
              err++;
              break;
            } else if( token( NULL, tok, NULL ) != COMMA ) {
              printf( "error:%d: expecting comma between operands\n", line );
              err++;
              break;
            } else if( token( NULL, tok, &r2 ) != REGISTER ) {
              printf( "error:%d: expecting register operand \n", line );
              err++;
              break;
            }
          } else if( XIS_NUM_OPS( ip->code ) == XIS_EXTENDED ) {
            rc = token( NULL, tok, &value );

            if( rc == LABEL ) {
              /* add addr reloc to be fixed up */
              err += !xreloc_reloc( relocs, pos + 2, XIS_ABS_SIZE, tok,
                                    XRELOC_ABSOLUTE );
            } else if( rc != INT ) {
              printf( "error:%d: expecting an integer or label operand\n",
                      line );
              err++;
              break;
            }

            if( ip->code & XIS_X_REG ) {
              if( token( NULL, tok, NULL ) != COMMA ) {
                printf( "error:%d: expecting comma between operands\n", line );
                err++;
                break;
              } else if( token( NULL, tok, &r1 ) != REGISTER ) {
                printf( "error:%d: expecting register operand \n", line );
                err++;
                break;
              }
            }
          }

          if( token( NULL, tok, NULL ) != NO_TOKEN ) {
            printf( "error:%d: unexpected token '%s' \n", line, tok );
            err++;
            break;
          }

          /* encoding
           */
          obj[pos] = ip->code;          /* get op code */
          pos++;
          obj[pos] = ( r1 << 4 ) | ( r2 & 0xf );
          pos++;

                                   /* if extended, encode value or address */
          if( XIS_NUM_OPS( ip->code ) == XIS_EXTENDED ) {
            obj[pos] = value >> 8;
            pos++;
            obj[pos] = value;
            pos++;
          }

          break;
        }
      }
    } else if( rc == DIRECTIVE ) {
      if( !strcmp( directives[D_LITERAL], tok ) ) {
        rc = token( NULL, tok, &value );
        if( rc == LITERAL ) {
          value = strlen( tok ) + 1;
          memcpy( &obj[pos], tok, value );
          pos += value;
          pos += value & 1;  /* round up to word boundary */
        } else if( rc == INT ) {
          if( value != (short)value ) {
            printf( "error:%d: integer %s is out of range \n", line, tok );
            err++;
            continue;
          }
          obj[pos] = value >> 8;
          pos++;
          obj[pos] = value;
          pos++;
        } else {
          printf( "error:%d: expecting string or integer\n", line );
          err++;
          continue;
        }
      } else if( !strcmp( directives[D_WORDS], tok ) ) {
        if( ( token( NULL, tok, &value ) != INT ) || ( (short)value < 1 ) ) {
          printf( "error:%d: expecting positive integer\n", line );
          err++;
          continue;
        }
        pos += value * 2;
      } else if( !strcmp( directives[D_GLOB], tok ) ) {
        if( token( NULL, tok, NULL ) != LABEL ) {
          printf( "error:%d: expecting label\n", line );
          err++;
          continue;
        }
        xreloc_global( relocs, tok );
      }
    } else {
      printf( "error:%d: expecting instruction or directive\n", line );
      err++;
      continue;
    }

    if( pos > XIS_MEM_SIZE ) {
      printf( "error:%d: program too large \n", line );
      err++;
      break;
    }
  }

  size = pos;                  /* size of object */

  if( !xreloc_relocate( relocs ) ) {
    err++;
  }

  size = xreloc_store_table( relocs, size, 0 );
  if( !size ) {
    err++;
  }

  xreloc_fini( relocs );

  /* check if errors have occurred
   */
  if( pos > XIS_MEM_SIZE ) {
    printf( "error: program too large \n" );
    err++;
  }

  if( err ) {
    printf( "%d errors occurred\n", err );
    return 1;
  } else {
    printf( "writing %d bytes\n", size );
  }


  unlink( argv[2] );           /* nuke old output file */

  fp = fopen( argv[2], "wb" ); /* open output file */
  if( !fp ) {
    printf( "error: could not open output file %s\n", argv[2] );
    return 1;
                               /* write output file */
  } else if( fwrite( obj, 1, size, fp ) < size ) {
    printf( "error: could not write output file %s\n", argv[2] );
    unlink( argv[2] );
  }
  fclose( fp );                /* close file */

  return 0;
}
