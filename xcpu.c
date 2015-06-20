#define XCPU
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "xis.h"
#include "xcpu.h"


//#define X_INSTRUCTIONS_NOT_NEEDED

#define WORD_SIZE 2    // word size in bytes
#define BYTE 8         // byte size in bits
#define MEMSIZE 0x10000


// a handy macro for fetching word-sized chunks of data from memory
#define FETCH_WORD(ptr) \
     (((unsigned short int) (c->memory[(ptr) % MEMSIZE] << 8)) & 0xFF00)\
     |(((unsigned short int) (c->memory[((ptr)+1) % MEMSIZE])) & 0x00FF)
// to replace deprecated fetch_word function, and save a bit of runtime.

// a helper macro for the various push-style instructions
#define PUSHER(word)\
  c->regs[15] -= 2;\
  c->memory[c->regs[15] % MEMSIZE] = (unsigned char) (word >> 8); \
  c->memory[(c->regs[15] + 1) % MEMSIZE] = (unsigned char) (word & 0xFF)

#define POPPER(dest) \
  dest = FETCH_WORD(c->regs[15]);\
  c->regs[15] += 2;
  

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


#include "xdb.c"

extern void xcpu_pretty_print(xcpu *c);
void xcpu_print(xcpu *c);
int xcpu_execute( xcpu *c, IHandler *table);
int xcpu_exception( xcpu *c, unsigned int ex );
unsigned short int fetch_word(unsigned char *mem, unsigned short int ptr);

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
/******************************************
 Display information about the xcpu context
 (Legacy debugger.)
*******************************************/
void xcpu_print( xcpu *c ) {
  int i;
  unsigned int op1;
  int op2;

  fprintf( stdout, "PC: %4.4x, State: %4.4x: Registers:\n", c->pc, c->state );
  for( i = 0; i < X_MAX_REGS; i++ ) {
    fprintf( stdout, " %4.4x", c->regs[i] );
  }
  fprintf( stdout, "\n" );

  op1 = c->memory[c->pc];
  op2 = c->memory[c->pc + 1];
  for( i = 0; i < I_NUM; i++ ) {
    if( x_instructions[i].code == c->memory[c->pc] ) {
      fprintf( stdout, "%s ", x_instructions[i].inst );
      break;
    }
  }

  switch( XIS_NUM_OPS( op1 ) ) {
  case 1:
    if( op1 & XIS_1_IMED ) {
      fprintf( stdout, "%d", op2 );
    } else {
      fprintf( stdout, "r%d", XIS_REG1( op2 ) );
    }
    break;
  case 2:
    fprintf( stdout, "r%d, r%d", XIS_REG1( op2 ), XIS_REG2( op2 ) );
    break;
  case XIS_EXTENDED:
    fprintf( stdout, "%u", (c->memory[c->pc + 2] << 8) | c->memory[c->pc + 3] );
    if( op1 & XIS_X_REG ) {
      fprintf( stdout, ", r%d", XIS_REG1( op2 ) );
    }
    break;
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
  if (_halt == 1 && (c->state & 0x2)) // check
    xcpu_print(c);
  if (MOREDEBUG)
    xcpu_pretty_print(c);
  return _halt;
}


/* Not needed for assignment 1 */
int xcpu_exception( xcpu *c, unsigned int ex ) {
  if (c->state | ~0x0004){ // IF NOT ALREADY IN INTERRUPT
    int i = ex * WORD_SIZE; // is this right?
    /* "i should be 0 if the exception is a regular interrupt; i should be
       2 if the exception is a trap; and i should be 4 if the exception is a
       fault." ex takes a value from the X_E_* enum defined in xcpu.h
       remember that WORD_SIZE = 2.
    */
    PUSHER(c->state);
    PUSHER(c->pc);
    c->state |= 0x0004;
    c->pc = fetch_word(c->memory, c->itr+i); // re: i, see comment above
  }
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
  _halt = 0; // global flag variable
}
INSTRUCTION(ret){
  POPPER(c->pc);
  /*c->pc = FETCH_WORD(c->regs[X_STACK_REG]);
    c->regs[X_STACK_REG] += WORD_SIZE;*/
}
INSTRUCTION(cld){
  c->state &= 0xfffd;
  // clear the debug bit
}
INSTRUCTION(std){
  c->state |= 0x0002;
}
INSTRUCTION(neg){
  c->regs[XIS_REG1(instruction)] = ~c->regs[XIS_REG1(instruction)]+1;
  // assuming 2'sC
}
INSTRUCTION(not){
  c->regs[XIS_REG1(instruction)] = !c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(push){
  PUSHER(c->regs[XIS_REG1(instruction)]);
}
INSTRUCTION(pop){
  c->regs[XIS_REG1(instruction)] = FETCH_WORD(c->regs[X_STACK_REG]);
  c->regs[X_STACK_REG] += WORD_SIZE;
}
INSTRUCTION(jmpr){
   c->pc = c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(callr){
  PUSHER(c->pc);
  c->pc = c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(out){
  fprintf(stdout, "%c", (char)(c->regs[XIS_REG1(instruction)] & 0xFF));
}
INSTRUCTION(inc){
  c->regs[XIS_REG1(instruction)]++;
}
INSTRUCTION(dec){
  c->regs[XIS_REG1(instruction)]--;
}
INSTRUCTION(br){
  signed char leap = instruction & 0x00FF;
  if (c->state & 0x0001) // if condition bit of state == 1
    c->pc = (c->pc-WORD_SIZE)+leap;
}
INSTRUCTION(jr){
  signed char leap = instruction & 0x00FF;
  c->pc = (c->pc-WORD_SIZE)+leap; // second byte of instruction = Label
}
INSTRUCTION(add){
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG1(instruction)] + c->regs[XIS_REG2(instruction)];
}
INSTRUCTION(sub){
    c->regs[XIS_REG2(instruction)] =
      c->regs[XIS_REG2(instruction)] - c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(mul){
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] * c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(divide){  
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] / c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(and){
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG1(instruction)] & c->regs[XIS_REG2(instruction)];
}
INSTRUCTION(or){
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] | c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(xor){
  c->regs[XIS_REG2(instruction)] =
    c->regs[XIS_REG2(instruction)] ^ c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(shr){
  c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG2(instruction)] >>
    c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(shl){
  c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG2(instruction)] <<
    c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(test){
  c->state = (c->regs[XIS_REG1(instruction)] & c->regs[XIS_REG2(instruction)])?
    (c->state | 0x0001) : (c->state & 0xFFFE);
}
INSTRUCTION(cmp){
  c->state = (c->regs[XIS_REG1(instruction)] < c->regs[XIS_REG2(instruction)])?
    (c->state | 0x0001) : (c->state & 0xFFFE);
  /** NB: cmp is only defined for unsigned integer values **/
}
INSTRUCTION(equ){
  c->state =
    (c->regs[XIS_REG1(instruction)]==c->regs[XIS_REG2(instruction)])?
    (c->state | 0x0001) :
    (c->state & 0xFFFE);
}
INSTRUCTION(mov){
  c->regs[XIS_REG2(instruction)] = c->regs[XIS_REG1(instruction)];
}
INSTRUCTION(load){
  c->regs[XIS_REG2(instruction)] =
    FETCH_WORD(c->regs[XIS_REG1(instruction)]);
}
INSTRUCTION(stor){
  c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)] >> 8));
  c->memory[(c->regs[XIS_REG2(instruction)]+1) % MEMSIZE] =
    (unsigned char) ((c->regs[XIS_REG1(instruction)]) & 0xFF);
}
INSTRUCTION(loadb){
  c->regs[XIS_REG2(instruction)] =
    c->memory[c->regs[XIS_REG1(instruction)] % MEMSIZE];
}
INSTRUCTION(storb){
  c->memory[c->regs[XIS_REG2(instruction)] % MEMSIZE] =
    c->regs[XIS_REG1(instruction)] & 0x00FF; // low byte mask
}
/*************************
 * extended instructions *
 *************************/
INSTRUCTION(jmp){
    c->pc = FETCH_WORD(c->pc);
}
INSTRUCTION(call){
  unsigned short int label = FETCH_WORD(c->pc);
  c->pc += WORD_SIZE;
  PUSHER(c->pc);
  c->pc = label; 
}
INSTRUCTION(loadi){
  unsigned short int value = FETCH_WORD(c->pc);
  c->pc += WORD_SIZE;
  c->regs[XIS_REG1(instruction) ] = value;
}
/*** INTERRUPT-HANDLING INSTRUCTIONS ***/
INSTRUCTION(cli){
  c->state &= 0xFFFB; // 0xFFFB == ~0x0004
}
INSTRUCTION(sti){
  c->state |= 0x0004;
}
INSTRUCTION(iret){
  /////
  printf(">>>>>>>>>>>>>>>>>>> pre IRET <<<<<<<<<<<<<<<<<<<<<<<<\n");

  printf(">>>>>>> PC = %4.4x  STATE = %4.4x  r15 = %4.4x -> %4.4x <<<<<<<<<<<<\n",c->pc, c->state, c->regs[X_STACK_REG], FETCH_WORD(c->regs[X_STACK_REG]));
  ///////////////////////////////

  POPPER(c->pc);
  POPPER(c->state);
  printf(">>>>>>>>>>>>>>>>>>> post IRET <<<<<<<<<<<<<<<<<<<<<<<<\n");

  printf(">>>>>>> PC = %4.4x  STATE = %4.4x  r15 = %4.4x -> %4.4x <<<<<<<<<<<<\n",c->pc, c->state, c->regs[X_STACK_REG], FETCH_WORD(c->regs[X_STACK_REG]));

}
/*
oINSTRUCTION(ret){
  c->pc = FETCH_WORD(c->regs[X_STACK_REG]);
  c->regs[X_STACK_REG] += WORD_SIZE;
  }*/
INSTRUCTION(trap){
  if (!(c->state & 0x0004)){
    xcpu_exception(c, X_E_TRAP); // very confusing.
    // in xcpu.h, xcpu_exception has the same description given to trap in
    // the instruction table on the hand-out. Doesn't make sense that the
    // same instructions are being performed twice in a row, and the
    // handout ALSO seems to have trap calling xcpu_exception. what a mess.
  }
}
INSTRUCTION(lit){
    c->itr = c->regs[XIS_REG1(instruction)];
}
  

/** That's all, folks! **/
unsigned char *sign="\xde\xba\x5e\x12";
