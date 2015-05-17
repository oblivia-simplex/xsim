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
#define MIN_PCB (32 + 1 + 1) 
#define TOP_OFF 1

typedef struct program *prog;
struct program {
  prog *next;
  int offset;
  int size;
  int rtab;
};

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
  xreloc relocs;
  struct stat *fs;
  unsigned char *obj;
  unsigned int offset;
  int size = XIS_MEM_SIZE;
  int csiz;
  unsigned int i;
  int num = argc - FILE_IDX;

  if( ( num < 1 )  ) {
    printf( "usage: xld <target> <main> [objs] ...\n" );
    printf( "       <target> combined object file\n" );
    printf( "       <main> object file where execution begins\n" );
    printf( "       [objs] zero or more object files to be linked\n" );
    return 1;
  } 

  fs = alloc( num * sizeof( struct stat ) );
  obj = alloc( XIS_MEM_SIZE );
  memset( obj, 0, XIS_MEM_SIZE );

  for( i = 0; i < num; i++ ) {
    if( stat( argv[i + FILE_IDX], &fs[i] ) ) {
      printf( "error: could not open file %s\n", argv[i + FILE_IDX] );
      return 1;
    }
    size -= fs[i].st_size;
    if( size < 0 ) {
      printf( "Not enough memory to load all the programs." );
      return 1;
    }
  }

  relocs = xreloc_init( obj, stdout );

  offset = 0; 
  for( i = 0; i < num; i++ ) {
    printf( "%20s at offset %d\n", argv[i + FILE_IDX], offset );
    fp = fopen( argv[i + FILE_IDX], "rb" );
    if( !fp ) {
      printf( "error: could not open file %s\n", argv[i + FILE_IDX] );
      return 1;
    } else if( fread( obj + offset, 1, fs[i].st_size, fp ) != fs[i].st_size ) {
      printf( "error: could not read file %s\n", argv[i + 2] );
      return 1;
    }

    csiz = xreloc_load_table( relocs, fs[i].st_size, offset );
    if( csiz < 1 ) {
      printf( "error: invalid object file: %s\n", argv[i + 2] );
      return 1;
    }
    offset += csiz;
  }
  printf( "%20s at offset %d\n", "Image end", offset );

  if( !xreloc_relocate( relocs ) ) {
    return 1;
  }
  xreloc_fini( relocs );

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
