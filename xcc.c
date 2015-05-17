#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_COMMSIZE  1024
#define XRT0 "xrt0.xo"
#define XAS_EXT ".xas"
#define XO_EXT ".xo"
#define XA_EXT ".xa"
#define XAS "xas"
#define XLD "xld"

int main( int argc, char **argv ) {

  char *comm;
  char *path;
  char *ext;
  char *target = NULL;
  char **objs;
  int num = 0;
  int err = 0;
  int i, rc, plen;

  objs = (char **) malloc( argc * sizeof( char * ) );
  comm = (char *) malloc( MAX_COMMSIZE );
  strcpy( comm, argv[0] );
  path = strrchr( comm, '/' );
  if( path ) {
    path++;
  } else {
    path = comm;
  }

  for( i = 1; i < argc; i++ ) {
    if( !strcmp( "-o", argv[i] ) ) {
      i++;
      if( i < argc ) {
        target = argv[i];
      } 
      continue;
    }

    objs[num] = strdup( argv[i] );
    ext = strrchr( objs[num], '.' );
    if( ext && !strcmp( XAS_EXT, ext ) ) {
      *ext = 0;
      sprintf( path, "%s %s%s %s%s", XAS, objs[num], XAS_EXT, objs[num], 
               XO_EXT );
      rc = system( comm );
      if( rc ) {
        printf( "error: xas returned %d\n", rc );
        err++;
      }
      strcpy( ext, XO_EXT );
      num++;
    } else if( ext && ( !strcmp( XO_EXT, ext ) || !strcmp( XA_EXT, ext ) ) ) {
      num++;
    } else {
      printf( "error: Unknown file or switch: %s\n", argv[i] );
      err++;
    }
  }
   
  if( target == NULL ) {
      printf( "error: no target specified\n" );
      err++;
  }

  if( err > 0 ) {
    return 1;
  }

  
  sprintf( path, "%s %s %s", XLD, target, XRT0 );
  plen = strlen( path );
  for( i = 0; i < num; i++ ) {
    sprintf( &path[plen], " %s", objs[i] );
    plen += strlen( &path[plen] );
  }

  rc = system( comm );
  if( rc ) {
    printf( "error: xld returned %d\n", rc );
    err++;
  }

  return err;
}
