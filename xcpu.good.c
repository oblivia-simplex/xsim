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
/** halt flag **/
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
void fatal(char* errmsg);

/**
 * The instruction functions are all constrained to have identical signatures,
 * since pointers to these functions need to be allocated in a jump table on
 * the heap. This lets us avoid a fair bit of code repetition, since we can
 * use a single macro to define their prototypes. The same macro can be used
 * for calling the functions. 
 **/
#define INSTRUCTION(x) void x(xcpu *c, unsigned short int instruction)

INSTRUCTION(bad);      INSTRUCTION(ret);      INSTRUCTION(cld);
INSTRUCTION(std);      INSTRUCTION(neg);      INSTRUCTION(not);
INSTRUCTION(push);     INSTRUCTION(pop);      INSTRUCTION(jmpr);
INSTRUCTION(callr);    INSTRUCTION(out);      INSTRUCTION(inc);
INSTRUCTION(dec);      INSTRUCTION(br);       INSTRUCTION(jr);
INSTRUCTION(add);      INSTRUCTION(sub);      INSTRUCTION(mul);
INSTRUCTION(divide);   INSTRUCTION(and);      INSTRUCTION(or);
INSTRUCTION(xor);      INSTRUCTION(shr);      INSTRUCTION(shl);
INSTRUCTION(test);     INSTRUCTION(cmp);      INSTRUCTION(equ);
INSTRUCTION(mov);      INSTRUCTION(load);     INSTRUCTION(stor);
INSTRUCTION(loadb);    INSTRUCTION(storb);    INSTRUCTION(jmp);
INSTRUCTION(call);     INSTRUCTION(loadi);

INSTRUCTION(cli);      INSTRUCTION(sti);      INSTRUCTION(iret);
INSTRUCTION(trap);     INSTRUCTION(lit);

/**************************************************************************
   Gracefully terminate the programme on unrecoverable error.
***************************************************************************/
void fatal(char *errmsg){
  fprintf(stderr, "%s\n", errmsg);
  exit(EXIT_FAILURE);  
  // some memory-freeing should happen here too...
}
/**************************************************************************
   Load a programme into memory, given a file descriptor. 
 **************************************************************************/
void load_programme(xcpu *c, FILE *fd){
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
  IHandler *table = malloc(0x100 * sizeof(IHandler)); 
  if (table == NULL){
    fprintf(stderr, "FAILURE IN <build_jump_table>: OUT OF MEMORY\n");
    exit(EXIT_FAILURE);
  }
  int p;
  for (p = 0; p < 0x100; p++){
    table[p] = bad;
  }
  table[I_BAD]  = bad;     table[I_RET]   = ret;     table[I_STD]   = std;
  table[I_NEG]  = neg;     table[I_NOT]   = not;     table[I_PUSH]  = push;
  table[I_POP]  = pop;     table[I_JMPR]  = jmpr;    table[I_CALLR] = callr;
  table[I_OUT]  = out;     table[I_INC]   = inc;     table[I_DEC]   = dec;
  table[I_BR]   = br;      table[I_JR]    = jr;      table[I_ADD]   = add;
  table[I_SUB]  = sub;     table[I_MUL]   = mul;     table[I_DIV]   = divide;
  table[I_AND]  = and;     table[I_OR]    = or;      table[I_XOR]   = xor;
  table[I_SHR]  = shr;     table[I_SHL]   = shl;     table[I_TEST]  = test;
  table[I_CMP]  = cmp;     table[I_EQU]   = equ;     table[I_MOV]   = mov;
  table[I_LOAD] = load;    table[I_STOR]  = stor;    table[I_LOADB] = loadb;
  table[I_STORB]= storb;   table[I_JMP]   = jmp;     table[I_CALL]  = call;
  table[I_LOADI]= loadi;   table[I_CLD]   = cld;
  // New instructions for handling interrupts (must be implemented):
  table[I_CLI]  = cli;     table[I_STI]   = sti;     table[I_IRET]  = iret;
  table[I_TRAP] = trap;    table[I_LIT]   = lit;
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
  unsigned short int instruction = FETCH_WORD(c->pc);
  //fetch_word(c->memory, c->pc); 
  opcode = (unsigned char)( (instruction >> 8) & 0x00FF); 
  c->pc += WORD_SIZE;  // extended instructions will increment pc a 2nd time
  (table[opcode])(c, instruction);
  if (_halt == 1 && (c->state & 0xFFFE))
    xcpu_print(c);
  if (MOREDEBUG)
    xcpu_pretty_print(c);
  return _halt;
}


/* Not needed for assignment 1 */
int xcpu_exception( xcpu *c, unsigned int ex ) {
  int i = ex * WORD_SIZE; // is this right?
  /* "i should be 0 if the exception is a regular interrupt; i should be
     2 if the exception is a trap; and i should be 4 if the exception is a
     fault." ex takes a value from the X_E_* enum defined in xcpu.h
     remember that WORD_SIZE = 2.
   */
  PUSHER(c->state);
  PUSHER(c->pc);
  c->state |= X_STATE_IN_EXCEPTION;
  c->pc = fetch_word(c->memory, c->itr+i); // re: i, see comment above

  return 1; // but returns 0 when not successful. How is this gauged?
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

INSTRUCTION(bad){
  if (MOREDEBUG) fprintf(LOG, "[BAD: signal to halt]\n");
  if (MOREDEBUG < 2)
    _halt = 0; // global flag variable
}
INSTRUCTION(ret){
  if (MOREDEBUG < 2){
    c->pc = FETCH_WORD(c->regs[X_STACK_REG]);
    c->regs[X_STACK_REG] += WORD_SIZE;
  }
  if (MOREDEBUG) fprintf(LOG, "[ret: return to <%4.4x>: %4.4x]\n",
                         c->pc, FETCH_WORD(c->pc));
}
INSTRUCTION(cld){
  if (MOREDEBUG) fprintf(LOG, "[cld]\n");
  if (MOREDEBUG < 2)
    c->state &= 0xfffd;
  // clear the debug bit
}
INSTRUCTION(std){
  if (MOREDEBUG) fprintf(LOG, "[std]\n");
  if (MOREDEBUG < 2)
    c->state |= 0x0002;
}
INSTRUCTION(neg){
  if (MOREDEBUG) fprintf(LOG, "[neg: negating %4.4x to %4.4x in R%d with 2'sC]\n",
                         c->regs[XIS_REG1(instruction)],
                         (unsigned short int)(~c->regs[XIS_REG1(instruction)]+1),
                         XIS_REG1(instruction));
  if (MOREDEBUG < 2)
    c->regs[XIS_REG1(instruction)] = ~c->regs[XIS_REG1(instruction)]+1;
  // assuming 2'sC
}
INSTRUCTION(not){
  if (MOREDEBUG) fprintf(LOG, "[not: logically negating %4.4x to %4.4x in R%d]\n",
                         c->regs[XIS_REG1(instruction)],
                         !c->regs[XIS_REG1(instruction)],
                         XIS_REG1(instruction));
  
  if (MOREDEBUG < 2)
    c->regs[XIS_REG1(instruction)] = !c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(push){
  if (MOREDEBUG < 2)
    PUSHER(c->regs[XIS_REG1(instruction)]);
  if (MOREDEBUG) fprintf(LOG,"[push: pushing %4.4x from reg %d onto stack @ %4.4x]\n",
                         c->regs[XIS_REG1(instruction)],
                         XIS_REG1(instruction),
                         c->regs[X_STACK_REG] );
}
INSTRUCTION(pop){
  if (MOREDEBUG) fprintf(LOG, "[pop: R%d <- %4.4x from top of stack]\n",
                         XIS_REG1(instruction),
                         FETCH_WORD(c->regs[X_STACK_REG]));
  if (MOREDEBUG < 2){
    c->regs[XIS_REG1(instruction)] = FETCH_WORD(c->regs[X_STACK_REG]);
    c->regs[X_STACK_REG] += WORD_SIZE;
  }
}
INSTRUCTION(jmpr){
 if (MOREDEBUG < 2)
   c->pc = c->regs[XIS_REG1(instruction)];
 if (MOREDEBUG) fprintf(LOG, "[jmpr: jumping to %4.4x (R%d)]\n",
                         c->regs[XIS_REG1(instruction)], XIS_REG1(instruction));
}
INSTRUCTION(callr){
  if (MOREDEBUG < 2){
    PUSHER(c->pc);
    c->pc = c->regs[XIS_REG1(instruction)];
  }
  if (MOREDEBUG) fprintf(LOG, "[callr: call to <%4.4x>: %2.2x%2.2x]\n",
                         c->regs[XIS_REG1(instruction)],
                         c->memory[c->regs[XIS_REG1(instruction)] %MEMSIZE],
                         c->memory[(c->regs[XIS_REG1(instruction)]+1)
                                   % MEMSIZE]);
}
INSTRUCTION(out){
  if (MOREDEBUG) fprintf(LOG, "[out: printing %c (0x%2.2x)] \n",
                         (char)(c->regs[XIS_REG1(instruction)] & 0xFF),
                         (char)(c->regs[XIS_REG1(instruction)] & 0xFF));
  if (MOREDEBUG < 2)
    fprintf(stdout, "%c", (char)(c->regs[XIS_REG1(instruction)] & 0xFF));
}
INSTRUCTION(inc){
  if (MOREDEBUG) fprintf(LOG, "[inc: increment R%d]\n",
                         XIS_REG1(instruction));
  if (MOREDEBUG < 2)
    c->regs[XIS_REG1(instruction)]++;
}
INSTRUCTION(dec){
  if (MOREDEBUG) fprintf(LOG, "[dec: decrement R%d]\n",
                         XIS_REG1(instruction));
  if (MOREDEBUG < 2) c->regs[XIS_REG1(instruction)]--;
}
INSTRUCTION(br){
  signed char leap = instruction & 0x00FF;
  if (MOREDEBUG) fprintf(LOG, "[br: branch %d to <%4.4x>: %4.4x if %1.1x == 1]\n",
                         leap,
                         (c->pc - WORD_SIZE)+leap,
                         FETCH_WORD((c->pc - WORD_SIZE) + leap),
                         (c->state & 0x0001));
  if (MOREDEBUG < 2)
    if (c->state & 0x0001) // if condition bit of state == 1
      c->pc = (c->pc-WORD_SIZE)+leap;
}
INSTRUCTION(jr){
  signed char leap = instruction & 0x00FF;
  if (MOREDEBUG) fprintf(LOG, "[jr: jump %d to <%4.4x>: %4.4x]\n",
                         leap,
                         (c->pc - WORD_SIZE)+leap,
                         FETCH_WORD((c->pc - WORD_SIZE)+leap));
  if (MOREDEBUG < 2)
    c->pc = (c->pc-WORD_SIZE)+leap; // second byte of instruction = Label
}
INSTRUCTION(add){
  if (MOREDEBUG) fprintf(LOG, "[add: R%d <- 0x%4.4x = 0x%4.4x + 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG1(instruction)] +
                          c->regs[XIS_REG2(instruction)]),
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] =
      c->regs[XIS_REG1(instruction)] + c->regs[XIS_REG2(instruction)];
}
INSTRUCTION(sub){
  if (MOREDEBUG) fprintf(LOG, "[sub: R%d <- 0x%4.4x = 0x%4.4x - 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG2(instruction)] -
                          c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] =
      c->regs[XIS_REG2(instruction)] - c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(mul){
  if (MOREDEBUG) fprintf(LOG, "[mul: R%d <- 0x%4.4x = 0x%4.4x * 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG1(instruction)] *
                          c->regs[XIS_REG2(instruction)]),
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] =
      c->regs[XIS_REG2(instruction)] * c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(divide){  
  if (MOREDEBUG) fprintf(LOG, "[div: R%d <- 0x%4.4x = 0x%4.4x / 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG2(instruction)] /
                          c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] =
      c->regs[XIS_REG2(instruction)] / c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(and){
  if (MOREDEBUG) fprintf(LOG, "[and: R%d <- 0x%4.4x = 0x%4.4x & 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG1(instruction)] &
                          c->regs[XIS_REG2(instruction)]),
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] =
      c->regs[XIS_REG1(instruction)] & c->regs[XIS_REG2(instruction)];
}
INSTRUCTION(or){
  if (MOREDEBUG) fprintf(LOG, "[or: R%d <- 0x%4.4x = 0x%4.4x | 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG2(instruction)] |
                          c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] =
      c->regs[XIS_REG2(instruction)] | c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(xor){
  if (MOREDEBUG) fprintf(LOG, "[xor: R%d <- 0x%4.4x = 0x%4.4x ^ 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         (c->regs[XIS_REG2(instruction)] ^
                          c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] =
      c->regs[XIS_REG2(instruction)] ^ c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(shr){
  if (MOREDEBUG) fprintf(LOG, "[shr: R%d <- 0x%4.4x = 0x%4.4x >> 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         c->regs[XIS_REG2(instruction)] >>
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG2(instruction)] >>
      c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(shl){
  if (MOREDEBUG) fprintf(LOG, "[shl: R%d <- 0x%4.4x = 0x%4.4x << 0x%4.4x]\n",
                         XIS_REG2(instruction),
                         c->regs[XIS_REG2(instruction)] <<
                         c->regs[XIS_REG1(instruction)],
                         c->regs[XIS_REG2(instruction)],
                         c->regs[XIS_REG1(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG2(instruction)] <<
      c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(test){
  if (MOREDEBUG) fprintf(LOG, "[test: set CONDITION flag if (R%d & R%d) is non-zero.]\n",
                         XIS_REG1(instruction),
                         XIS_REG2(instruction));
  if (MOREDEBUG < 2)
    c->state = (c->regs[XIS_REG1(instruction)] & c->regs[XIS_REG2(instruction)])?
      (c->state | 0x0001) : (c->state & 0xFFFE);
}
INSTRUCTION(cmp){
  if (MOREDEBUG) fprintf(LOG, "[cmp: set CONDITION flag if value in R%d (%4.4x) < value in R%d (%4.4x)]\n",
                         XIS_REG1(instruction),
                         c->regs[XIS_REG1(instruction)],
                         XIS_REG2(instruction),
                         c->regs[XIS_REG2(instruction)]);
  if (MOREDEBUG < 2)
    c->state = (c->regs[XIS_REG1(instruction)] < c->regs[XIS_REG2(instruction)])?
      (c->state | 0x0001) : (c->state & 0xFFFE);
  /** NB: cmp is only defined for unsigned integer values **/
}
INSTRUCTION(equ){
  if (MOREDEBUG)
    fprintf(LOG, "[equ: set CONDITION flag if value in R%d"
            " == value in R%d]\n", XIS_REG1(instruction),
            XIS_REG2(instruction));
  if (MOREDEBUG < 2)
    c->state =
      (c->regs[XIS_REG1(instruction)]==c->regs[XIS_REG2(instruction)])?
      (c->state | 0x0001) :
      (c->state & 0xFFFE);
}
INSTRUCTION(mov){
  c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG1(instruction)];
  if (MOREDEBUG) fprintf(LOG, "[mov: copy %4.4x from R%d to R%d]\n",
                         c->regs[XIS_REG1(instruction)],
                         XIS_REG1(instruction),
                         XIS_REG2(instruction));
}
INSTRUCTION(load){
  if (MOREDEBUG) fprintf(LOG, "[load: put %4.4x from memory[%4.4x] into R%d]\n",
                         FETCH_WORD(c->regs[XIS_REG1(instruction)]),
                         c->regs[XIS_REG1(instruction)],
                         XIS_REG2(instruction));
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] =
      FETCH_WORD(c->regs[XIS_REG1(instruction)]);
}
INSTRUCTION(stor){
  if (MOREDEBUG < 2){
    c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE] =
      (unsigned char) ((c->regs[XIS_REG1(instruction)] >> 8));
    c->memory[(c->regs[XIS_REG2(instruction)]+1) % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)]) & 0xFF);
  }
  if (MOREDEBUG) fprintf(LOG, "[stor: storing %2.2x%2.2x in memory[%4.4x], from R%d]\n",
                         (unsigned char)  (c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE]),
                         (unsigned char) (c->memory[(c->regs[XIS_REG2(instruction)]+1) % MEMSIZE]),
                         c->regs[XIS_REG2(instruction) % MEMSIZE],
                         XIS_REG1(instruction));
}
INSTRUCTION(loadb){
  if (MOREDEBUG) fprintf(LOG, "[loadb: loading byte %2.2x (%c) into R%d from memory[%4.4x] ]\n",
                         c->memory[c->regs[XIS_REG1(instruction)]%MEMSIZE],
                         prchar(c->memory[c->regs[XIS_REG1(instruction)]%MEMSIZE]),
                         XIS_REG2(instruction),
                         c->regs[XIS_REG2(instruction)]);
  if (MOREDEBUG < 2)
    c->regs[XIS_REG2(instruction)] =
      c->memory[c->regs[XIS_REG1(instruction)] % MEMSIZE];
}
INSTRUCTION(storb){
  if (MOREDEBUG) fprintf(LOG, "[storb: storing byte %2.2x (%c) in memory[%4.4x] from R%d]\n",
                         c->regs[XIS_REG1(instruction)] & 0x00FF,
                         prchar(c->regs[XIS_REG1(instruction)] & 0x00FF),
                         c->regs[XIS_REG2(instruction)]%MEMSIZE,
                         XIS_REG1(instruction));
  if (MOREDEBUG < 2)
    c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE] =
      c->regs[XIS_REG1(instruction)] & 0x00FF; // low byte mask
}
/*************************
 * extended instructions *
 *************************/
INSTRUCTION(jmp){
  if (MOREDEBUG) fprintf(LOG, "[[jmp]]\n");
  if (MOREDEBUG < 2)
    c->pc = FETCH_WORD(c->pc);
}
INSTRUCTION(call){
  unsigned short int label = FETCH_WORD(c->pc);
  if (MOREDEBUG < 2){
    c->pc += WORD_SIZE;
    PUSHER(c->pc);
    c->pc = label; 
  }
  if (MOREDEBUG) fprintf(LOG, "[[call to <%4.4x>: %2.2x%2.2x]]\n",
                         label,
                         (c->memory[label % MEMSIZE]),
                         (c->memory[(label+1) % MEMSIZE]));
}
INSTRUCTION(loadi){
  unsigned short int value = FETCH_WORD(c->pc);
  if (MOREDEBUG < 2){
    c->pc += WORD_SIZE;
    c->regs[XIS_REG1(instruction) ] = value;
  }
  if (MOREDEBUG) fprintf(LOG, "[[loadi: loading %4.4x into register R%d]]\n",
                           value, XIS_REG1(instruction));
}
/*** INTERRUPT-HANDLING INSTRUCTIONS ***/
INSTRUCTION(cli){
  if (MOREDEBUG) fprintf(LOG, "[cli: Clearing interrupt bit.]\n");
  if (MOREDEBUG < 2)
    c->state &= 0xFFFB;
}
INSTRUCTION(sti){
  if (MOREDEBUG) fprintf(LOG, "[sti: Setting interrupt bit.]\n");
  if (MOREDEBUG < 2)
    c->state |= 0x0004;
}
INSTRUCTION(iret){
  if (MOREDEBUG) fprintf(LOG, "[iret: returning from interrupt.]\n");
  if (MOREDEBUG < 2){
    c->pc = FETCH_WORD(c->regs[X_STACK_REG]);
    c->regs[X_STACK_REG] += WORD_SIZE;
    c->state = FETCH_WORD(c->regs[X_STACK_REG]);
    c->regs[X_STACK_REG] += WORD_SIZE;
  }
}
INSTRUCTION(trap){
  if (MOREDEBUG) fprintf(LOG, "[trap]\n");
  if (MOREDEBUG < 2)
    if (!(c->state & 0x0004)){
      xcpu_exception(c, X_E_TRAP); // very confusing.
    // in xcpu.h, xcpu_exception has the same description given to trap in
    // the instruction table on the hand-out. Doesn't make sense that the
    // same instructions are being performed twice in a row, and the
    // handout ALSO seems to have trap calling xcpu_exception. what a mess.
  }
}
INSTRUCTION(lit){
  if (MOREDEBUG) fprintf(LOG, "[lit: ITR <- 0x%4.4x from R%d]\n",
                         c->regs[XIS_REG1(instruction)],
                         XIS_REG1(instruction));
  if (MOREDEBUG < 2)
    c->itr = c->regs[XIS_REG1(instruction)];
}
  

/** That's all, folks! **/
unsigned char *sign="\xde\xba\x5e\x12";
