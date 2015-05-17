#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define X_INSTRUCTIONS_NOT_NEEDED

#include "xis.h"
#include "xreloc.h"

#define INV_ADDR    (XIS_MEM_SIZE - 1)

#define FLAG_GLOBAL     0x00000001
#define FLAG_WRITTEN    0x00000002
#define FLAG_RELOCAT    0x00000004
#define GETWORD(m,x)    ((m[(x)] << 8) | (m[(x) + 1]))
#define RELOC_SIZE 4
#define REL_NAME "$"

typedef struct reloc_struct reloc;
struct reloc_struct {
  reloc          *next;
  int            loc;
  int            size;
  unsigned char  flags;
};

typedef struct symbol_struct symbol;
struct symbol_struct {              /* stores a list of relocs for a symbol */
  symbol *next;                     /* next symbol in the list */
  int   loc;                        /* location of symbol in code */
  reloc *relocs;                    /* list of relocs */
  int   flags;                      /* flags */
  int   size;                       /* size of relocs to put into table */
  char  name[1];                    /* symbol name */
};

typedef struct table_struct table;
struct table_struct {
  unsigned char *mem;
  symbol        *syms;
  FILE          *err;
  int           rel_cnt;
};


static symbol *findsym( table *t, char *name );
static void *memalloc( int size, FILE *err );
static void add_reloc( table *t, symbol *sym, int loc, int size, 
                       unsigned char flags );


xreloc *xreloc_init( unsigned char *mem, FILE *err ) {
  table *t;

  assert( mem );

  if( !err ) {
    err = stderr;
  }
  t = memalloc( sizeof( table ), err );
  t->mem = mem;
  t->err = err;
  return (void *)t;
}


void xreloc_global( xreloc xr, char *name ) {
  table *t = (table *) xr;
  symbol *sym;

  assert( t );
  assert( name );

  sym = findsym( t, name );
  assert( sym );
  sym->flags |= FLAG_GLOBAL;
}


int xreloc_symbol( xreloc xr, int loc, char *name ) {
  table *t = (table *) xr;
  symbol *sym;

  assert( t );
  assert( name );

  sym = findsym( t, name );
  assert( sym );

  if( sym->loc != INV_ADDR ) {
    fprintf( t->err, "error: symbol '%s' is redefined\n", name );
    return 0;
  }

  sym->loc = loc;
  return 1;
}


int xreloc_reloc( xreloc xr, int loc, int size, char *name, 
                  unsigned char flags ) {
  table *t = (table *) xr;
  symbol *sym;

  assert( t );
  assert( name );

  if( loc == INV_ADDR ) {
    fprintf( stderr, "error: invalid address '%x', object may be too big\n", 
                     INV_ADDR );
    return 0;
  }

  sym = findsym( t, name );
  assert( sym );

  add_reloc( t, sym, loc, size, flags );
  return 1;
}


int  xreloc_load_table( xreloc xr, int size, int base ) {
  table *t = (table *) xr;
  int i, ent, loc;
  short chksum = 0;
  int code_size = 0;
  symbol *sym;
  char buf[10];

  assert( t );
  assert( size >= 0 );
  assert( base >= 0 );
  assert( ( base + size ) <= XIS_MEM_SIZE );

  if( size & 1 ) { /* Cannot have a valid table because size is odd */
    return -1;
  }

  for( i = 0; i < size; i += 2 ) {
    chksum += GETWORD( t->mem, i + base );
  }

  if( chksum ) { /* chksum failed, no table */
    return -1;
  }
 
  i = GETWORD( t->mem, size + base - 4 );
  if( i != XIS_VERSION ) {
    fprintf( t->err, "error: unknown version %x\n", i );
    return 0;
  }

  ent = GETWORD( t->mem, size + base - 6 ) + base + 1;  /* 1 is the padding */
  code_size = GETWORD( t->mem, size + base - 6 );

  while( t->mem[ent] && ( ent < ( base + size ) ) ) {
    if( !strcmp( REL_NAME, (char *)&t->mem[ent] ) ) {
      sprintf( buf, "%s%x", REL_NAME, t->rel_cnt );
      t->rel_cnt++;
      sym = findsym( t, buf );
      sym->flags = FLAG_RELOCAT;
    } else {
      sym = findsym( t, (char *) &t->mem[ent] );
    }
    ent += strlen( (char *) &t->mem[ent] ) + 1;

    loc = GETWORD( t->mem, ent );
    ent += 2;
    if( loc != INV_ADDR ) {
      if( sym->loc == INV_ADDR ) {
        sym->loc = loc + base;
      } else {
        fprintf( t->err, "error: Multiple instances of symbol '%s'\n", 
                 sym->name );
        code_size = 0;
      }
    } 

    for( loc = GETWORD( t->mem, ent ); loc != INV_ADDR; 
         loc = GETWORD( t->mem, ent ) ) {
      ent += 2;
      i = t->mem[ent++] & 0xff;
      add_reloc( t, sym, loc + base, i, t->mem[ent++] );
    }
    ent += 2;
  }

  return code_size;
}


int  xreloc_store_table( xreloc xr, int size, int base ) {
  table *t = (table *) xr;
  int i, ent;
  short chksum = 0;
  symbol *sym;
  reloc *r;

  assert( t );
  assert( size >= 0 );
  assert( base >= 0 );

  ent = base + size;
  if( ent >= XIS_MEM_SIZE ) {
    fprintf( t->err, "error: Out of space\n" );
    return 0;
  }
  t->mem[ent++] = 0;

  for( sym = t->syms; sym; sym = sym->next ) {
    if( sym->flags & FLAG_GLOBAL ) {
      if( ( ent + sym->size ) >= XIS_MEM_SIZE ) {
        fprintf( t->err, "error: Out of space\n" );
        return 0;
      }
      sym->flags |= FLAG_WRITTEN;

      strcpy( (char *)&t->mem[ent], sym->name );
      ent += strlen( sym->name ) + 1;
      t->mem[ent++] = sym->loc >> 8;
      t->mem[ent++] = sym->loc;
       
      for( r = sym->relocs; r; r = r->next ) {
        t->mem[ent++] = r->loc >> 8;
        t->mem[ent++] = r->loc;
        t->mem[ent++] = r->size;
        t->mem[ent++] = r->flags;
      }
      t->mem[ent++] = 0xff;
      t->mem[ent++] = 0xff;
    }
  }
  t->mem[ent++] = 0;

  if( ( ent + XIS_TRAILER_SIZE ) > XIS_MEM_SIZE ) {
    fprintf( t->err, "error: Out of space\n" );
    return 0;
  }

  ent += ( ent - base ) & 1;
  t->mem[ent++] = size >> 8;
  t->mem[ent++] = size;
  t->mem[ent++] = XIS_VERSION >> 8;
  t->mem[ent++] = XIS_VERSION & 0xff;

  for( i = base; i < ent; i += 2 ) {
    chksum += GETWORD( t->mem, i + base );
  }
  chksum = -chksum;
  t->mem[ent++] = chksum >> 8;
  t->mem[ent++] = chksum;

  return ent - base;
}


int  xreloc_relocate( xreloc xr ) {
  table *t = (table *) xr;
  int loc;
  int word;
  int mask;
  int err = 0;
  symbol *sym;
  symbol *rel = NULL;
  reloc *r;

  rel = findsym( t, REL_NAME );
  rel->flags = FLAG_RELOCAT | FLAG_GLOBAL;
  rel->loc = 0;

  for( sym = t->syms; sym; sym = sym->next ) {
    if( sym->flags & FLAG_GLOBAL ) { /* symbols to be written to symbol table */
      continue;
    } else if( sym->flags & FLAG_RELOCAT ) { /* symbols need to be relocated */
      sym->flags |= FLAG_WRITTEN;
      for( r = sym->relocs; r; r = r->next ) { 
        add_reloc( t, rel, r->loc, r->size, XRELOC_ABSOLUTE );

        word = GETWORD( t->mem, r->loc );
        mask = ( 1 << r->size ) - 1;
        loc = ( word & mask ) + sym->loc;
        if( loc & ~mask ) {
          fprintf( t->err, "error: relocation out of range for symbol '%s'\n", 
                   sym->name );
          err = 1;
        }
        word = ( word & ~mask ) | ( loc & mask );
        t->mem[r->loc] = word >> 8;
        t->mem[r->loc + 1] = word;
      }
    } else if( sym->loc != INV_ADDR ) { /* symbol is defined */
                               /* loop through all relocs for symbol */
      sym->flags |= FLAG_WRITTEN;
      for( r = sym->relocs; r; r = r->next ) { 
        loc = sym->loc;
        if( r->flags & XRELOC_RELATIVE ) {
          loc -= r->loc;
          if( ( loc < -( 1 << ( r->size - 1 ) ) ) || 
              ( loc > ( 1 << ( r->size - 1 ) ) - 1 ) ) {
            fprintf( t->err, "error: relocation out of range for symbol '%s'\n",
                     sym->name );
            err = 1;
          }
        } else {
          add_reloc( t, rel, r->loc, r->size, XRELOC_ABSOLUTE );
          if( loc > ( ( 1 << r->size ) - 1 ) ) {
            fprintf( t->err, "error: relocation out of range for symbol '%s'\n",
                     sym->name );
            err = 1;
          }
        }

        word = GETWORD( t->mem, r->loc );
        mask = ( 1 << r->size ) - 1;
        word = ( word & ~mask ) | ( loc & mask );
        t->mem[r->loc] = word >> 8;
        t->mem[r->loc + 1] = word;
      }
    } else { /* symbol was undefined */
      fprintf( t->err, "error: symbol '%s' used but undefined\n", sym->name );
      err = 1;
    }
  }

  return !err;
}


int  xreloc_fini( xreloc xr ) {
  table *t = (table *) xr;
  symbol *sym;
  reloc *r;

  for( sym = t->syms; sym; sym = t->syms ) {
    t->syms = sym->next;
    for( r = sym->relocs; r; r = sym->relocs ) {
      sym->relocs = r->next;
      free( r );
    }
    free( sym );
  }
  free( t );


  return 1;
}   


static symbol *findsym( table *t, char *name ) {
  symbol *sym;

  for( sym = t->syms; sym; sym = sym->next ) {
    if( !strcmp( sym->name, name ) ) {
      return sym;
    }
  }

  sym = memalloc( sizeof( struct symbol_struct ) + strlen( name ) + 1, t->err );
  sym->next = t->syms;
  t->syms = sym;
  strcpy( sym->name, name );
  sym->loc = INV_ADDR;
  return sym;
}


static void *memalloc( int size, FILE *err ) {
  void *p;

  assert( size > 0 );

  p = calloc( size, 1 );
  if( !p ) {
    fprintf( err, "Out of Memory\n" );
    exit( 1 );
  }
  return p;
}


static void add_reloc( table *t, symbol *sym, int loc, int size,
                       unsigned char flags ) {
  reloc *r;

  assert( t );
  assert( sym );
  assert( loc != INV_ADDR );

  r = memalloc( sizeof( reloc ), t->err );
  r->loc = loc;
  r->flags = flags;
  r->size = size;
  r->next = sym->relocs;
  sym->relocs = r; 
  sym->size += RELOC_SIZE;
}
