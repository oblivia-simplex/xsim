#include <stdio.h>
#include <stdlib.h>

#include "xcpu.c"
//#include "xcpu.h"
#include "xdb.c"

/**
 * MOREDEBUG turns on a host of helpful debugging features, which I 
 * gradually pieced together for my own benefit, in trying to get 
 * this programme to run. Features actived by the MOREDEBUG macro
 * switch are: a pretty and much-improved xcpu_print called 
 * xcpu_pretty_print, on-the-fly natural language disassembly and
 * running commentary on the active instruction in each cycle, and
 * a cycle counter. The default channel for all of this is stderr,
 * leaving stdout unsullied, and still usable for comparing with the
 * output of xsim_gold. To temporarily turn off the MOREDEBUG features,
 * just redirect stderr to /dev/null on the command line. To more 
 * permanently turn it off, just switch it to 0 below and recompile. 
 * For an optimised version of xcpu, I may consider trimming the fat
 * and commenting out all of the MOREDEBUG features, since, taken
 * together, they linearly increase the runtime relative to the number
 * of cycles (since, even when deactived, they mean another if-statement
 * or two to evaluate for every machine instruction). Since this cpu is
 * being developed for experimentation rather than for speed, however, 
 * it seems advisable to leave the MOREDEBUG features intact.  
 **/
#define MOREDEBUG 1


#define TICK_ARG 1
#define IMAGE_ARG 2
#define QUANTUM_ARG 3

void init_cpu(xcpu *c);
FILE* load_file(char *filename);
int load_programme(xcpu *c, FILE *fd);
void shutdown(xcpu *c);

int main(int argc, char *argv[]){
  if (argc != 4){
    printf("Usage: %s <cycles> <filename> <interrupt frequency>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // parse command-line options
  int cycles = atoi(argv[TICK_ARG]);
  FILE *fd=load_file(argv[IMAGE_ARG]);
  int interrupt_freq = atoi(argv[QUANTUM_ARG]);

  
  xcpu *c = malloc(sizeof(xcpu));
  init_cpu(c);
  load_programme(c, fd); // loads the bytes of fd into c->memory

  // the IHandler type is defined in xcpu.h, and the IHandler jump table
  // is implemented in xcpu.c // instruction handler, not interrupt handler.
  // consider renaming to avoid confusion!
  IHandler *table = build_jump_table();
  // It is a jump table to the functions handling each instruction.

  char *graceful    = "CPU has halted.";
  char *out_of_time = "CPU ran out of time.";
  int halted, i=0;
  while (i++ < cycles || !cycles){
    if (MOREDEBUG) fprintf(LOG, "<CYCLE %d>\n",i-1);
    if (i != 0 && interrupt_freq != 0 && i % interrupt_freq == 0)
      if (!xcpu_exception(c, X_E_INTR)){
        fprintf(stderr, "Exception error at 0x%4.4x. CPU has halted.\n",
                c->pc);
        exit(EXIT_FAILURE);
      }
    if (halted = !xcpu_execute(c, table)) break;
    if (MOREDEBUG)
      xcpu_pretty_print(c);

  } // on halt, halted gets 0; otherwise halted remains 1
  char *exit_msg = (halted)? graceful : out_of_time;
  fprintf(stdout, "%s\n", exit_msg);
  fprintf(LOG, "(%d cycles completed.)\n", i-1);
  destroy_jump_table(table);
  shutdown(c);
  return !halted;
}

FILE* load_file(char* filename){
  FILE *fd;
  if ((fd = fopen(filename, "rb")) == NULL){
    char msg[80]="error: could not stat image file ";
    strncat(msg, filename,30);
    fatal(msg);
  }
  return fd;
}


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
    fatal(msg);
  }
  return addr;
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

void shutdown(xcpu *c){
  free(c->memory);
  free(c);
}
