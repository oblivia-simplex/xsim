#ifndef XRELOC_H
#define XRELOC_H

#define XRELOC_ABSOLUTE 0x00
#define XRELOC_RELATIVE 0x01

typedef void *xreloc;

extern xreloc *xreloc_init( unsigned char *mem, FILE *fp );
extern void xreloc_global( xreloc xr, char *name );
extern int  xreloc_symbol( xreloc xr, int loc, char *name );
extern int  xreloc_reloc( xreloc xr, int loc, int size, char *name, 
                           unsigned char flags );

extern int  xreloc_load_table( xreloc xr, int size, int base );
extern int  xreloc_store_table( xreloc xr, int size, int base );
extern int  xreloc_relocate( xreloc xr );
extern int  xreloc_fini( xreloc xr );

#endif
