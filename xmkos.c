#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define X_INSTRUCTIONS_NOT_NEEDED

#include "xis.h"
#include "xreloc.h"

#define FILE_IDX 2
#define PROC_LOC_SIZE 1 /* one word */

static void *alloc( int size ) {
  void *p = malloc( size );
  if( !p ) {
    printf( "error: memory allocation (%d) failed\n", size );
    exit( 1 );
  }
  return p;
}

int main( int argc, char **argv ) {

  FILE *fp;
  struct stat *fs;
  unsigned char *obj;
  unsigned int offset;
  unsigned short *tab;
  int size = XIS_MEM_SIZE;
  int tab_size;
  int csiz;
  unsigned int i;
  int num = 0;
  xreloc relocs;

  if( argc < 2 ) {
    printf( "usage: xmkos <image> <kernel obj file> [process obj file] ...\n" );
    printf( "       <image> combined image object file\n" );
    printf( "       <kernel obj file> assembled kernel xis file\n" );
    printf( "       [process obj files] assembled process xis files\n" );
    return 1;
  }

  fs = alloc( argc * sizeof( struct stat ) );
  obj = alloc( XIS_MEM_SIZE );
  memset( obj, 0, XIS_MEM_SIZE );

  for( i = FILE_IDX; i < argc; i++ ) {
    if( stat( argv[i], &fs[i] ) ) {
      printf( "error: could not open file %s\n", argv[i] );
      return 1;
    }
    size -= fs[i].st_size;
    if( size < 0 ) {
      printf( "Not enough memory to load all the programs." );
      return 1;
    }
    num++;
  }

  tab_size = num * PROC_LOC_SIZE * sizeof( unsigned short ) +
             sizeof( unsigned short );
  size -= tab_size;
  if( size < 0 ) {
    printf( "Not enough memory to load all the programs." );
    return 1;
  }

  offset = 0;
  for( i = FILE_IDX; i < argc; i++ ) {
    printf( "%20s at offset %d\n", argv[i], offset );
    fp = fopen( argv[i], "rb" );
    if( !fp ) {
      printf( "error: could not open file %s\n", argv[i] );
      return 1;
    } else if( fread( obj + offset, 1, fs[i].st_size, fp ) != fs[i].st_size ) {
      printf( "error: could not read file %s\n", argv[i] );
      return 1;
    }

    relocs = xreloc_init( obj, stdout );
    csiz = xreloc_load_table( relocs, fs[i].st_size, offset );
    if( csiz < 1 ) {
      printf( "error: invalid object file: %s\n", argv[i] );
      return 1;
    } else if( !xreloc_relocate( relocs ) ) {
      printf( "error: relocation failure in file: %s\n", argv[i] );
      return 1;
    }
    xreloc_fini( relocs );

    if( !offset ) {
      printf( "%20s at offset %d\n", "Process Table", offset + (int) csiz );
      offset += tab_size;
      tab = (unsigned short *)( obj + csiz );
    } else {
      tab[0] = htons( offset );
      tab += PROC_LOC_SIZE;
    }
    fclose( fp );
    offset += csiz;
  }
  printf( "%20s at offset %d\n", "Image end", offset );
  tab[0] = htons( offset );
  tab[1] = 0xffff;

  fp = fopen( argv[1], "wb" );
  if( !fp ) {
    printf( "error: could not open image file '%s'\n", argv[1] );
    return 1;
  } else if( fwrite( obj, 1, offset, fp ) != offset ) {
    printf( "error: could not write image file '%s'\n", argv[1] );
    return 1;
  }
  fclose( fp );

  return 0;
}
