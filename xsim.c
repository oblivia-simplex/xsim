#include <stdio.h>
#include <stdlib.h>

#include "xcpu.c"

//void shutdown(xcpu *c);
void init_cpu(xcpu *c);
FILE* load_file(char *filename);
extern void load_programme(xcpu *c, FILE *fd);

int main(int argc, char *argv[]){
  if (argc != 3){
    printf("Usage: %s <cycles> <filename>\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  int cycles;
  xcpu *c = malloc(sizeof(xcpu));
  init_cpu(c);
  FILE *fd=load_file(argv[2]);
  load_programme(c, fd); // loads the bytes of fd into c->memory
  cycles = atoi(argv[1]);

  // the IHandler type is defined in xcpu.h, and the IHandler jump table
  // is implemented in xcpu.c
  IHandler *table = build_jump_table();
  // It is a jump table to the functions handling each instruction.
  char *graceful    = "CPU has halted.";
  char *out_of_time = "CPU is out of time.";
  int halted, i=0;
  while (i++ < cycles || !cycles){
    if (MUCHDEBUG) fprintf(LOG, "<CYCLE %d>\n",i-1);
    if (halted = !xcpu_execute(c, table)) break; 
  } // on halt, halted gets 0; otherwise halted remains 1
  char *exit_msg = (halted)? graceful : out_of_time;
  fprintf(stdout, "%s\n", exit_msg);
  fprintf(LOG, "(%d cycles completed.)\n", i-1);

  destroy_jump_table(table);
  //shutdown(c);
  return !halted;
}

FILE* load_file(char* filename){
  FILE *fd = fopen(filename, "rb");
  return fd;
}

void init_cpu(xcpu *c){
  c->memory = calloc(MEMSIZE, sizeof(unsigned char));
  // MEMSIZE def in xcpu.c
  // c->regs   ;//calloc(X_MAX_REGS, sizeof(unsigned short int));
  unsigned char i;
  for(i=0; i<=15; c->regs[i++]=0)
    ;
  c->pc     = 0x0;
  c->state  = 0x0;
  c->itr    = 0x0;
  c->id     = 0x0;
  c->num    = 0x1;
}
/*
  void shutdown(xcpu *c){
  free(c->memory);
  free(c->regs);
  free(c);
  }*/
