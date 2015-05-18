#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "xis.h"
#include "xcpu.h"

#define X_INSTRUCTIONS_NOT_NEEDED

#define WORD_SIZE 2    // word size in bytes
#define BYTE 8         // byte size in bits
#define MEMSIZE 0x10000

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

#define LOG stderr

int _halt = 1; // set to 0 when BAD instruction encountered.

char prchar(char c);
void xcpu_pretty_print(xcpu *c);
void xcpu_print(xcpu *c);
int xcpu_execute( xcpu *c, IHandler *table);
int xcpu_exception( xcpu *c, unsigned int ex );
unsigned short int fetch_word(unsigned char *mem, unsigned short int ptr);
void load_programme(xcpu *c, FILE *fd);
void * build_jump_table(void);
void destroy_jump_table(void * addr);

// instruction set
void bad(xcpu *c, unsigned short int instruction);
void ret(xcpu *c, unsigned short int instruction);
void cld(xcpu *c, unsigned short int instruction);
void std(xcpu *c, unsigned short int instruction);
void neg(xcpu *c, unsigned short int instruction);
void not(xcpu *c, unsigned short int instruction);
void push(xcpu *c, unsigned short int instruction);
void pop(xcpu *c, unsigned short int instruction);
void jmpr(xcpu *c, unsigned short int instruction);
void callr(xcpu *c, unsigned short int instruction);
void out(xcpu *c, unsigned short int instruction);
void inc(xcpu *c, unsigned short int instruction);
void dec(xcpu *c, unsigned short int instruction);
void br(xcpu *c, unsigned short int instruction);
void jr(xcpu *c, unsigned short int instruction);
void add(xcpu *c, unsigned short int instruction);
void sub(xcpu *c, unsigned short int instruction);
void mul(xcpu *c, unsigned short int instruction);
void divi(xcpu *c, unsigned short int instruction);
void and(xcpu *c, unsigned short int instruction);
void or(xcpu *c, unsigned short int instruction);
void xor(xcpu *c, unsigned short int instruction);
void shr(xcpu *c, unsigned short int instruction);
void shl(xcpu *c, unsigned short int instruction);
void test(xcpu *c, unsigned short int instruction);
void cmp(xcpu *c, unsigned short int instruction);
void equ(xcpu *c, unsigned short int instruction);
void mov(xcpu *c, unsigned short int instruction);
void load(xcpu *c, unsigned short int instruction);
void stor(xcpu *c, unsigned short int instruction);
void loadb(xcpu *c, unsigned short int instruction);
void storb(xcpu *c, unsigned short int instruction);
void jmp(xcpu *c, unsigned short int instruction);
void call(xcpu *c, unsigned short int instruction);
void loadi(xcpu *c, unsigned short int instruction);

/****************************************************************************
   Load a programme into memory, given a file descriptor. 
 ****************************************************************************/
void load_programme(xcpu *c, FILE *fd){
  unsigned int addr = 0;
  while ((c->memory[addr++ % MEMSIZE] = fgetc(fd)) != 0xFF)  
    ;
}
/***************************************************************************
   CREATE A JUMP TABLE TO THE INSTRUCTION FUNCTIONS
   =-=-=-=-=-=-=-=-=-=-=-==-=-=-=-=-=-=-=-=-=-=-=-=
   To be called only *once* per process. This should save time, as compared
   to letting the compiler construct a jump table (as its interpretation of
   a switch/case sequence) each time xcpu_execute is called.
   (Called from xsim.c, just prior to the first execution loop.)
**************************************************************************/
void* build_jump_table(void){
  IHandler *table = malloc(0xFF * sizeof(IHandler)); 
  if (table == NULL){
    fprintf(stderr, "FAILURE IN <build_jump_table>: OUT OF MEMORY\n");
    exit(EXIT_FAILURE);
  }
  unsigned char p;
  for (p = 0; p < 0xFF; p++){
    table[p] = bad;
  }
  table[I_BAD]  = bad;     table[I_RET]   = ret;     table[I_STD]   = std;
  table[I_NEG]  = neg;     table[I_NOT]   = not;     table[I_PUSH]  = push;
  table[I_POP]  = pop;     table[I_JMPR]  = jmpr;    table[I_CALLR] = callr;
  table[I_OUT]  = out;     table[I_INC]   = inc;     table[I_DEC]   = dec;
  table[I_BR]   = br;      table[I_JR]    = jr;      table[I_ADD]   = add;
  table[I_SUB]  = sub;     table[I_MUL]   = mul;     table[I_DIV]   = divi;
  table[I_AND]  = and;     table[I_OR]    = or;      table[I_XOR]   = xor;
  table[I_SHR]  = shr;     table[I_SHL]   = shl;     table[I_TEST]  = test;
  table[I_CMP]  = cmp;     table[I_EQU]   = equ;     table[I_MOV]   = mov;
  table[I_LOAD] = load;    table[I_STOR]  = stor;    table[I_LOADB] = loadb;
  table[I_STORB]= storb;   table[I_JMP]   = jmp;     table[I_CALL]  = call;
  table[I_LOADI]= loadi;  
  return table;
}
/*************************************************
 * Free the memory allocated to the jump table.  *
 * To be called once, at the end of the process. *
 *************************************************/
void destroy_jump_table(void* table){
  free(table);
}
char prchar(char c){
  return ('A' <= c && c <= 'z')? c : '.';
}
/*****************************************************************************
 * Display *thorough* information about the current CPU context, incuding 
 * register contents and nearby code and stack memory. The channel used
 * by xcpu_pretty_print is stderr, by default. This allows us to let
 * pretty_print run while still collecting the legacy debugging output from
 * stdout, for purposes of comparison with xsim_gold.
 *****************************************************************************/
void xcpu_pretty_print(xcpu *c){
  int i;
  char * cnd = "CONDITION ";
  char * dbg = "DEBUG ";
  char flags[17] = "";
  if (c->state & 0x0001) strcat(flags,cnd);
  if (c->state & 0xFFFE) strcat(flags,dbg); // a bit redundant...
                                
  fprintf(LOG, "PC: %4.4x, State: %4.4x ( Flags: %s)"
          "\n-------=oO( REGISTERS )Oo=-----------------------------------------------------\n", c->pc, c->state, flags );
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
}
/******************************************
 Display information about the xcpu context
 (Legacy debugger.)
*******************************************/
void xcpu_print( xcpu *c ) {
  // xcpu_pretty_print is a thorough (& attractive) debugging module, which
  // prints to stderr by default (output channel is controlled by the macro
  // LOG, defined at the head of this file).
  if (MOREDEBUG) xcpu_pretty_print(c);  
  int i;
  fprintf( stdout, "PC: %4.4x, State: %4.4x: Registers:\n", c->pc, c->state );
  for( i = 0; i < X_MAX_REGS; i++ ) {
    fprintf( stdout, " %4.4x", c->regs[i] );
  }
  fprintf( stdout, "\n" );

}
/************************************************************
 Read the opcode, and use it as an index into the jump table. 
 Continue returning 1 until the programme halts (and the global
 _halt variable is set to 0), and which point return 0. The
 actual 'halting' is handled by xsim.c.
*************************************************************/
int xcpu_execute(xcpu *c, IHandler *table) {

  unsigned char opcode;
  unsigned short int instruction = fetch_word(c->memory, c->pc); 
  opcode = (unsigned char)( (instruction >> 8) & 0x00FF); 
  c->pc += WORD_SIZE;  //////
  (table[opcode])(c, instruction);
  if (_halt == 1 && (c->state & 0xFFFE))
    xcpu_print(c);
  else if (MOREDEBUG)
    xcpu_pretty_print(c);
  return _halt;
}
/* Not needed for assignment 1 */
int xcpu_exception( xcpu *c, unsigned int ex ) {
  fprintf(stderr, "FATAL: xcpu_exception.\n");
  xcpu_print(c);
  exit(EXIT_FAILURE);
}
/******************************************************************
   Constructs a word out of two contiguous bytes in a byte array,
   and returns it. Can be used to fetch instructions, labels, and
   immediate values.
 ******************************************************************/
unsigned short int fetch_word(unsigned char *mem, unsigned short int ptr){
  unsigned short int word =
    (((unsigned short int) (mem[ptr % MEMSIZE] << 8)) & 0xFF00)
    |(((unsigned short int) (mem[(ptr+1) % MEMSIZE]))   & 0x00FF);
  return word;
}

/*********************************
 * THE INSTRUCTION SET FUNCTIONS *
 *********************************/
    
void bad(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[BAD: signal to halt]\n");
  _halt = 0; // global flag variable
}
void ret(xcpu *c, unsigned short int instruction){
  c->pc = fetch_word(c->memory,c->regs[X_STACK_REG]);
  c->regs[X_STACK_REG] += WORD_SIZE;
  if (MOREDEBUG) fprintf(LOG, "[ret: return to <%4.4x>: %4.4x]\n",
                         c->pc,
                         fetch_word(c->memory, c->pc));
}
void cld(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[cld]\n");
  c->state &= 0xfffd;
  // clear the debug bit
}
void std(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[std]\n");
  c->state |= 0x0002;
}
void neg(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[neg: negating %4.4x to %4.4x in R%d with 2'sC]\n",
                         c->regs[XIS_REG1(instruction)],
                         (unsigned short int)(~c->regs[XIS_REG1(instruction)]+1),
                         XIS_REG1(instruction));
  c->regs[XIS_REG1(instruction)] = ~c->regs[XIS_REG1(instruction)]+1;
  // assuming 2'sC
}
void not(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[not: logically negating %4.4x to %4.4x in R%d]\n",
                         c->regs[XIS_REG1(instruction)],
                         !c->regs[XIS_REG1(instruction)],
                         XIS_REG1(instruction));
  
  c->regs[XIS_REG1(instruction)] = !c->regs[XIS_REG1(instruction)];
}
void push(xcpu *c, unsigned short int instruction){
  c->regs[X_STACK_REG] -= WORD_SIZE;
  c->memory[c->regs[X_STACK_REG] % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)] >> 8));
  c->memory[(c->regs[X_STACK_REG]+1) % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)]) & 0xFF);
  if (MOREDEBUG) fprintf(LOG,"[push: pushing %4.4x from reg %d onto stack @ %4.4x]\n",
                         c->regs[XIS_REG1(instruction)],
                         XIS_REG1(instruction),
                         c->regs[X_STACK_REG] );
}
void pop(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[pop: R%d <- %4.4x from top of stack]\n",
                         XIS_REG1(instruction),
                         fetch_word(c->memory,
                                    (c->regs[X_STACK_REG] % MEMSIZE)));
  c->regs[XIS_REG1(instruction)] =
    fetch_word(c->memory, c->regs[X_STACK_REG]);
  c->regs[X_STACK_REG] += WORD_SIZE;
}
void jmpr(xcpu *c, unsigned short int instruction){
  c->pc = c->regs[XIS_REG1(instruction)];
  if (MOREDEBUG) fprintf(LOG, "[jmpr: jumping to %4.4x (R%d)]\n",
                         c->regs[XIS_REG1(instruction)], XIS_REG1(instruction));
}
void callr(xcpu *c, unsigned short int instruction){
  c->regs[X_STACK_REG] -= WORD_SIZE;
  c->memory[c->regs[X_STACK_REG] % MEMSIZE]     = (c->pc >> 8);
  c->memory[(c->regs[X_STACK_REG]+1) % MEMSIZE] = (c->pc & 0xFF);
  c->pc = c->regs[XIS_REG1(instruction)];
  if (MOREDEBUG) fprintf(LOG, "[callr: call to <%4.4x>: %2.2x%2.2x]\n",
                         c->regs[XIS_REG1(instruction)],
                         c->memory[c->regs[XIS_REG1(instruction)] %MEMSIZE],
                         c->memory[(c->regs[XIS_REG1(instruction)]+1)
                                   % MEMSIZE]);
}
void out(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[out: printing %c (0x%2.2x)] \n",
                         (char)(c->regs[XIS_REG1(instruction)] & 0xFF),
                         (char)(c->regs[XIS_REG1(instruction)] & 0xFF));
  fprintf(stdout, "%c", (char)(c->regs[XIS_REG1(instruction)] & 0xFF));
}
void inc(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[inc: increment R%d]\n",
                         XIS_REG1(instruction));
  c->regs[XIS_REG1(instruction)]++;
}
void dec(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[dec: decrement R%d]\n",
                         XIS_REG1(instruction));
  c->regs[XIS_REG1(instruction)]--;
}
void br(xcpu *c, unsigned short int instruction){
  signed char leap = instruction & 0x00FF;
  if (MOREDEBUG) fprintf(LOG, "[br: branch %d to <%4.4x>: %4.4x if %1.1x == 1]\n",
                         leap,
                         (c->pc - WORD_SIZE)+leap,
                         fetch_word(c->memory, (c->pc - WORD_SIZE)+leap),
                         (c->state & 0x0001));
  if (c->state & 0x0001) // if condition bit of state == 1
    c->pc = (c->pc-WORD_SIZE)+leap;
}
void jr(xcpu *c, unsigned short int instruction){
  signed char leap = instruction & 0x00FF;
  if (MOREDEBUG) fprintf(LOG, "[jr: jump %d to <%4.4x>: %4.4x]\n",
                         leap,
                         (c->pc - WORD_SIZE)+leap,
                         fetch_word(c->memory, (c->pc - WORD_SIZE)+leap));
  c->pc = (c->pc-WORD_SIZE)+leap; // second byte of instruction = Label
}
void add(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[add: R%d <- 0x%4.4x = 0x%4.4x + 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG1(instruction)] +
                          c->regs[XIS_REG2(instruction)]),
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)]);
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG1(instruction)] + c->regs[XIS_REG2(instruction)];
}
void sub(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[sub: R%d <- 0x%4.4x = 0x%4.4x - 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG2(instruction)] -
                          c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] - c->regs[XIS_REG1(instruction)];
}
void mul(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[mul: R%d <- 0x%4.4x = 0x%4.4x * 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG1(instruction)] *
                          c->regs[XIS_REG2(instruction)]),
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)]);
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] * c->regs[XIS_REG1(instruction)];
}
void divi(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[div: R%d <- 0x%4.4x = 0x%4.4x / 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG2(instruction)] /
                          c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] / c->regs[XIS_REG1(instruction)];
}
void and(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[and: R%d <- 0x%4.4x = 0x%4.4x & 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG1(instruction)] &
                          c->regs[XIS_REG2(instruction)]),
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)]);
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG1(instruction)] & c->regs[XIS_REG2(instruction)];
}
void or(xcpu *c, unsigned short int instruction){ 
  if (MOREDEBUG) fprintf(LOG, "[or: R%d <- 0x%4.4x = 0x%4.4x | 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG2(instruction)] |
                          c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] | c->regs[XIS_REG1(instruction)];
}
void xor(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[xor: R%d <- 0x%4.4x = 0x%4.4x ^ 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG2(instruction)] ^
                          c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] ^ c->regs[XIS_REG1(instruction)];
}
void shr(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[shr: R%d <- 0x%4.4x = 0x%4.4x >> 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         c->regs[XIS_REG2(instruction)] >>
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG2(instruction)] >>
    c->regs[XIS_REG1(instruction)];
}
void shl(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[shl: R%d <- 0x%4.4x = 0x%4.4x << 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         c->regs[XIS_REG2(instruction)] <<
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG2(instruction)] <<
    c->regs[XIS_REG1(instruction)];
}
void test(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[test: set CONDITION flag if either R%d or R%d hold non-zero value]\n",
                         XIS_REG1(instruction),
                         XIS_REG2(instruction));
  c->state = (c->regs[XIS_REG1(instruction)] & c->regs[XIS_REG2(instruction)])?
    (c->state | 0x0001) : (c->state & 0xFFFE);
}

void cmp(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[cmp: set CONDITION flag if value in R%d < value in R%d]\n",
                         XIS_REG1(instruction),
                         XIS_REG2(instruction));
  c->state = (c->regs[XIS_REG1(instruction)] < c->regs[XIS_REG2(instruction)])?
    (c->state | 0x0001) : (c->state & 0xFFFE);
}
void equ(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG)
    fprintf(LOG, "[equ: set CONDITION flag if value in R%d"
            " == value in R%d]\n", XIS_REG1(instruction),
            XIS_REG2(instruction));
  c->state =
    (c->regs[XIS_REG1(instruction)]==c->regs[XIS_REG2(instruction)])?
    (c->state | 0x0001) :
    (c->state & 0xFFFE);
}
void mov(xcpu *c, unsigned short int instruction){
  c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG1(instruction)];
  if (MOREDEBUG) fprintf(LOG, "[mov: copy %4.4x from R%d to R%d]\n",
                         c->regs[XIS_REG1(instruction)],
                         XIS_REG1(instruction),
                         XIS_REG2(instruction));
}
void load(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[load: put %4.4x from memory[%4.4x] into R%d]\n",
                         fetch_word(c->memory, c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG1(instruction)],
                         XIS_REG2(instruction));
  c->regs[XIS_REG2(instruction)] =
    fetch_word(c->memory, c->regs[XIS_REG1(instruction)]);
}
void stor(xcpu *c, unsigned short int instruction){

  c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)] >> 8));
  c->memory[(c->regs[XIS_REG2(instruction)]+1) % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)]) & 0xFF);
  if (MOREDEBUG) fprintf(LOG, "[stor: storing %2.2x%2.2x in memory[%4.4x], from R%d]\n",
                         //c->regs[XIS_REG1(instruction)],
                         (unsigned char)  (c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE]),
                         (unsigned char) (c->memory[(c->regs[XIS_REG2(instruction)]+1) % MEMSIZE]),
                         c->regs[XIS_REG2(instruction) % MEMSIZE],
                         XIS_REG1(instruction));
}
void loadb(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[loadb: loading byte %2.2x (%c) into R%d from memory[%4.4x] ]\n",
                         c->memory[c->regs[XIS_REG1(instruction)]%MEMSIZE],
                         prchar(c->memory[c->regs[XIS_REG1(instruction)]%MEMSIZE]),
                         XIS_REG2(instruction),
                         c->regs[XIS_REG2(instruction)]);
  c->regs[XIS_REG2(instruction)] =
    c->memory[c->regs[XIS_REG1(instruction)] % MEMSIZE];
}
void storb(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[storb: storing byte %2.2 (%c) in memory[%4.4x] from R%d]\n",
                         c->regs[XIS_REG1(instruction)] & 0x00FF,
                         prchar(c->regs[XIS_REG1(instruction)] & 0x00FF),
                         c->regs[XIS_REG2(instruction)]%MEMSIZE,
                         XIS_REG1(instruction));
  c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE] =
    c->regs[XIS_REG1(instruction)] & 0x00FF; // low byte mask
}
/*************************
 * extended instructions *
 *************************/
void jmp(xcpu *c, unsigned short int instruction){
  if (MOREDEBUG) fprintf(LOG, "[[jmp]]\n");
  c->pc = fetch_word(c->memory, c->pc);
}
void call(xcpu *c, unsigned short int instruction){
  unsigned short int label = fetch_word(c->memory, c->pc);
  c->pc += WORD_SIZE;
  c->regs[X_STACK_REG] -= WORD_SIZE;
  c->memory[(c->regs[X_STACK_REG]) % MEMSIZE] =
    (unsigned char) (c->pc >> 8);
  c->memory[(c->regs[X_STACK_REG]+1) % MEMSIZE] =
    (unsigned char) (c->pc & 0xFF);
  c->pc = label; 
  if (MOREDEBUG) fprintf(LOG, "[[call to <%4.4x>: %2.2x%2.2x]]\n",
                         label,
                         (c->memory[label % MEMSIZE]),
                         (c->memory[(label+1) % MEMSIZE]));
}
void loadi(xcpu *c, unsigned short int instruction){
  unsigned short int value = fetch_word(c->memory, c->pc);
  c->pc += WORD_SIZE;
  c->regs[XIS_REG1(instruction) ] = value;
    if (MOREDEBUG) fprintf(LOG, "[[loadi: loading %4.4x into register R%d]]\n",
                           value, XIS_REG1(instruction));
}
/** That's all, folks! **/
