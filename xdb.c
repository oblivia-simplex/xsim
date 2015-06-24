#ifndef XCPU
#define XCPU
#include "xcpu.c"
#endif
char prchar(char c);
void xdumper(xcpu *c);
void disas(xcpu *c);
char ** build_disas_table(void);
void xcpu_pretty_print(xcpu *c);


/************************************************************************
 * Returns ASCII character, if parameter is within printable range, and otherwise
 * returns '.'.
 ************************************************************************/
char prchar(char c){
  return ('A' <= c && c <= 'z')? c : '.';
}

/************************************************************************
 * Takes pointer to CPU context as parameter, and prints disassembly of
 * instruction at PC, as side effect, to LOG (stderr by default). 
 ************************************************************************/
void disas(xcpu *c){
  unsigned short int instruction;
  instruction = FETCH_WORD(c->pc);

  unsigned char opcode = (unsigned char)( (instruction >> 8) & 0x00FF);
  char *reg1, *reg1_val, *reg2, *reg2_val, *label, *numval; // tidy up
  char istring[80] = "";
  char operand[80] = "";
  char mnemonic[8];
  int i;
  
  for(i = 0; i < I_NUM; i++) {
    if(x_instructions[i].code == c->memory[c->pc] ) {
      sprintf(mnemonic, "%s ", x_instructions[i].inst );
      break;
    }
  }
  strcat(istring,mnemonic);
  strcat(istring,"\t");
  
  //fprintf(LOG,"%s",mnemonic);
  char o2[8];
  switch (XIS_NUM_OPS(opcode)){
  case 0:
    break;
  case 1: // opcode or instruction here?
    if (opcode & XIS_1_IMED)
      sprintf(operand, "0x%4.4x", (instruction & 0xFF)); // save as string
    else
      sprintf(operand, "r%d\t\t# 0x%4.4x", XIS_REG1(instruction),
              c->regs[XIS_REG1(instruction)]);
    break;
  case 2:
    sprintf(operand,   "r%d,r%d\t\t# 0x%4.4x, 0x%4.4x", XIS_REG1(instruction),
            XIS_REG2(instruction), c->regs[XIS_REG1(instruction)],
            c->regs[XIS_REG2(instruction)]);
    break;    
  case XIS_EXTENDED:
    sprintf(operand,   "0x%4.4x", FETCH_WORD(c->pc+WORD_SIZE));
    if (opcode & XIS_X_REG){
      sprintf(o2, ", r%d", XIS_REG1(instruction));
      strcat(operand, o2);
    }
    break;
  }
  //fprintf(LOG, "%s", operand);
  strcat(istring, operand);
  fprintf(LOG, "%s\n", istring);
}

/*************************************************************************
 * Disassembles and prints the instruction at c->pc. Can be called either
 * during live debugging, or called in a loop from the xdump utility, to
 * disassemble the object code from start to finish (similar to objdump). 
 *************************************************************************/
void xdumper(xcpu *c){
  unsigned char opcode = (unsigned char)(( (FETCH_WORD(c->pc)) >> 8) & 0x00FF);
  disas(c);
  c->pc += (XIS_NUM_OPS(opcode) == XIS_EXTENDED)? 2*WORD_SIZE : WORD_SIZE;
}


/*****************************************************************************
 * Display *thorough* information about the current CPU context, incuding 
 * register contents and nearby code and stack memory. The channel used
 * by xcpu_pretty_print is stderr, by default. This allows us to let
 * pretty_print run while still collecting the legacy debugging output from
 * stdout, for purposes of comparison with xsim_gold.
 *****************************************************************************/
void xcpu_pretty_print(xcpu *c){
  static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
  
  if( pthread_mutex_lock( &lk ) ) {
    fprintf(LOG, "*** Failure to acquire lock! ***\n" );
    abort();
  }


  int i;
  char * cnd = "CONDITION ";
  char * dbg = "DEBUG ";
  char * intr = "INTERRUPT ";
  char flags[17] = "";

  if (c->state & 0x0001) strcat(flags,cnd);
  if (c->state & 0xFFFE) strcat(flags,dbg); // a bit redundant...
  if (c->state & 0x0004) strcat(flags,intr);                              
  fprintf(LOG, "CPU: %2.2d | PC: %4.4x | State: %4.4x | ITR: %4.4x \nFlags: %s)"
          "\n-------=oO( REGISTERS )Oo=-----------------------------------------------------\n", c->id, c->pc, c->state, c->itr, flags );
  for( i = 0; i < X_MAX_REGS; i++ ) {
    fprintf(LOG, "%4.4x", c->regs[i] );
    if (i < X_MAX_REGS-1) fprintf(LOG," ");
  }
  fprintf(LOG, "\n" );
  fprintf(LOG, "-------=oO( STACK )Oo=---------------------------------------------------------\n");
  for( i = 0; i < 0x1e; i+=2){
    fprintf(LOG, "%4.4x: %2.2x %2.2x | ",
            (c->regs[X_STACK_REG]+i) % MEMSIZE,
            c->memory[(c->regs[X_STACK_REG]+i) % MEMSIZE],
            c->memory[(c->regs[X_STACK_REG]+i+1) % MEMSIZE]);
    if ((i+2)% 0xA ==0 | i == 0x1f) fprintf(LOG, "\n");
  }
  for( i = 0; i < 0x1e; i+=2){
    fprintf(LOG, "%4.4x: %c %c | ",
            (c->regs[X_STACK_REG]+i) % MEMSIZE,
            prchar(c->memory[(c->regs[X_STACK_REG]+i) % MEMSIZE]),
            prchar(c->memory[(c->regs[X_STACK_REG]+i+1) % MEMSIZE]));
    if ((i+2)% 0xA ==0 | i == 0x1f) fprintf(LOG, "\n");
  }
  fprintf(LOG,"-------=oO( CODE )Oo=----------------------------------------------------------\n");
  for(i=0; i < 0x1e; i+=2){
    fprintf(LOG, "%4.4x: %2.2x %2.2x | ",
            c->pc+i,
            c->memory[(c->pc+i) % MEMSIZE],
            c->memory[(c->pc+i+1) % MEMSIZE]);
    if ((i+2)% 0xA ==0 | i == 0x1f) fprintf(LOG, "\n");
  }
  for(i=0; i < 0x1e; i+=2){
    fprintf(LOG, "%4.4x: %c %c | ",
            c->pc+i,
            prchar(c->memory[(c->pc+i) % MEMSIZE]),
            prchar(c->memory[(c->pc+i+1) % MEMSIZE]));
    if ((i+2)% 0xA ==0 | i == 0x1f) fprintf(LOG, "\n");
  }
  fprintf(LOG,"*******************************************************************************\n");
  fprintf(LOG,"NEXT: "); disas(c);
  fprintf(LOG,"*******************************************************************************\n");

  if( pthread_mutex_unlock( &lk ) ) {
    fprintf(LOG, "*** Failure to release lock! ***\n" );
    abort();
  }
}
