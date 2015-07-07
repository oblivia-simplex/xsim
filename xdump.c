#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "xis.h"
#include "xcpu.h"
#define XCPU

#define LOG stdout
#define WORD_SIZE 2    // word size in bytes
#define BYTE 8         // byte size in bits
#define MEMSIZE 0x10000

// a handy macro for fetching word-sized chunks of data from memory
#define FETCH_WORD(ptr) \
     (((unsigned short int) (c->memory[(ptr) % 0x1000] << 8)) & 0xFF00)\
     |(((unsigned short int) (c->memory[((ptr)+1) % 0x1000])) & 0x00FF)
// to replace deprecated fetch_word function, and save a bit of runtime.

// a helper macro for the various push-style instructions
#define PUSHER(word)\
  c->regs[15] -= 2;\
  c->memory[c->regs[15] % 0x10000] = (unsigned char) (word >> 8); \
  c->memory[(c->regs[15] + 1) % 0x10000] = (unsigned char) (word & 0xFF)



// to prevent xdb from reloading the xcpu module
#include "xdb.c"


/**
 * A handy utility for disassembling x object code -- something like a simplified
 * version of objdump for the xcpu. 
 **/

void init_cpu(xcpu *c);
FILE* load_file(char *filename);
int load_programme(xcpu *c, FILE *fd); // in xcpu.c, loaded from xdb.c

int main(int argc, char **argv){
  int enumerate = 0; // boolean
  if (argc < 2){
    printf("Usage %s <.x or .xo object file>\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  if (argc >= 3){
    if (argv[2][0] = '-')
      switch (argv[2][1]){
      case 'c':
        enumerate = 1;
        break;
      default:
        break;
      }
  }
  FILE *fd = load_file(argv[1]);

  xcpu *c = malloc(sizeof(xcpu));
  if (c == NULL){
    fprintf(stderr, "Out of memory.\n");
    exit(EXIT_FAILURE);
  }

  init_cpu(c);
  int plen, counter;
  plen = load_programme(c,fd);



  // loop condition to loop through entire file. (till c->pc >= plen)
  while (c->pc <= plen){
    //printf(".");
    xdumper(c, enumerate);
  }
  return 0;
}


// CODE DUPLICATION FROM XSIM.C -- SHOULD PUT THESE IN A HEADER FILE

/**************************************************************************
   Load a programme into memory, given a file descriptor. 
 **************************************************************************/
int load_programme(xcpu *c, FILE *fd){
  unsigned int addr = 0;
  do
    c->memory[addr++] = fgetc(fd);
  while (!feof(fd) && addr < MEMSIZE);
  if (addr >= MEMSIZE){
    char msg[70]; 
    sprintf(msg, "Programme larger than %d bytes. Too big to fit in memory.",
            MEMSIZE);
   fprintf(stderr, "%s\n",msg);
    exit(EXIT_FAILURE);
  }
  return addr;
}


FILE* load_file(char* filename){
  FILE *fd;
  
  if ((fd = fopen(filename, "rb")) == NULL){
    char msg[80]="error: could not stat image file ";
    strncat(msg, filename,30);
    fprintf(stderr, "%s\n",msg);
    exit(EXIT_FAILURE);
  }
  
  return fd;
}

void init_cpu(xcpu *c){
  c->memory = calloc(MEMSIZE, sizeof(unsigned char));
  unsigned char i;
  for(i=0; i<=15; c->regs[i++]=0) // registers will need to be on the stack
    ;                             // so that each thread can maintain its own
  c->pc     = 0x0;
  c->state  = 0x0;
  c->itr    = 0x0;
  c->id     = 0x0;
  c->num    = 0x1;
}
