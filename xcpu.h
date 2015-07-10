#ifndef XCPU_H
#define XCPU_H


#define X_MAX_REGS 16                 /* maximum size of the data stack */


/* CPU context struct */

#define X_STATE_COND_FLAG     0x0001 /* Last comparison result       */
#define X_STATE_DEBUG_ON      0x0002 /* CPU is is debug mode         */
#define X_STATE_IN_EXCEPTION  0x0004 /* CPU is handling an exception */

#define X_STACK_REG           15     /* stack register */

typedef struct xcpu_context {        
  unsigned char *memory;              /* pointer to shared memory segment */
  unsigned short regs[X_MAX_REGS];    /* general register file */
  unsigned short state;               /* state register */
  unsigned short itr;                 /* interrupt table register */
  unsigned short id;                  /* cpu identifier */
  unsigned short num;                 /* number of cpus */
  unsigned short pc;                  /* program counter */
  /** moved pc to bottom of struct, to guard against buffer overflow vulns **/
} xcpu;


/**
 * Pointer to jump table of instruction-handling functions. 
 **/
typedef void (*IHandler)(xcpu*, unsigned short int);

/* title: execute next instruction
 * param: pointer to CPU context
 * function: performs instruction indexed by the program counter 
 * returns: 1 if successful, 0 if not
 */
extern int xcpu_execute( xcpu *c, IHandler *table );


// i should be 0 if regular interrupt, 2 if trap, 4 if fault, so enum*2?
enum {
  X_E_INTR,    /* Interrupt has occurred */ // 0 
  X_E_TRAP,    /* Trap has occurred */      // 1
  X_E_FAULT,   /* Fault has occurred */     // 2
  X_E_LAST                                  // 3  
};

/* title: generate an exception (interrupt, trap, or fault)
 * param: pointer to CPU context, type of exception
 *        (X_E_INTR, X_E_TRAP, X_E_FAULT)
 * function: generates the exception, pushes current state register and
             pc on stack, jumps to handler, sets the X_STATE_IN_EXCEPTION
 *           bit in state register
 * returns: 1 if successful, 0 if not
 */
extern int xcpu_exception( xcpu *c, unsigned int ex );

/** ADDED BY OLF **/
//#define X_INSTRUCTIONS_NOT_NEEDED

#define WORD_SIZE 2    // word size in bytes
#define BYTE 8         // byte size in bits
#define MEMSIZE 0x10000//0x10000
#define LOG stderr

/******************************************************************
   Constructs a word out of two contiguous bytes in a byte array,
   and returns it. Can be used to fetch instructions, labels, and
   immediate values.
******************************************************************/
#define FETCH_WORD(ptr)                                                 \
  (((unsigned short int) (c->memory[(ptr) % MEMSIZE] << 8)) & 0xFF00)   \
  |(((unsigned short int) (c->memory[((ptr)+1) % MEMSIZE])) & 0x00FF)

// a helper macro for the various push-style instructions
#define PUSHER(word)                                                    \
  c->regs[15] -= 2;                                                     \
  c->memory[c->regs[15] % MEMSIZE] = (unsigned char) (word >> 8);       \
  c->memory[(c->regs[15] + 1) % MEMSIZE] = (unsigned char) (word & 0xFF)

// helper macro for pop-style instructions
#define POPPER(dest)                            \
  dest = FETCH_WORD(c->regs[15]);               \
  c->regs[15] += 2;

// Global xmpsim lock variable, "execution lock". 
static pthread_mutex_t elk = PTHREAD_MUTEX_INITIALIZER; 

#define LOCK(lockname) \
  if (pthread_mutex_lock(&(lockname))){ \
    fprintf(LOG, "-=-= FAILURE TO ACQUIRE LOCK! =-=-\n"); \
    abort();\
  } 

#define UNLOCK(lockname) \
  if (pthread_mutex_unlock(&(lockname))){ \
    fprintf(LOG, "-=-= FAILURE TO RELEASE LOCK! =-=-\n"); \
    abort(); \
  } 


/********************** FUNCTION PROTOTYPES **********************/

extern void xcpu_pretty_print(xcpu *c);
extern void xcpu_print(xcpu *c);
//int xcpu_execute( xcpu *c, IHandler *table);
//int xcpu_exception( xcpu *c, unsigned int ex );
extern void * build_jump_table(void);
extern void destroy_jump_table(void * addr);
extern void fatal(char* errmsg);

/**
 * The instruction functions are all constrained to have identical signatures,
 * since pointers to these functions need to be allocated in a jump table on
 * the heap. This lets us avoid a fair bit of code repetition, since we can
 * use a single macro to define their prototypes. The same macro can be used
 * for calling the functions. 
 **/
#define INSTRUCTION(x)  void x(xcpu *c, unsigned short int instruction)
// instructions added in Assignment 1 (base set)
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
// instructions added in Assignment 2 (interrupt handling)
INSTRUCTION(cli);      INSTRUCTION(sti);      INSTRUCTION(iret);
INSTRUCTION(trap);     INSTRUCTION(lit);
// instructions added in Assignment 3 (threading)
INSTRUCTION(cpuid);    INSTRUCTION(cpunum);   INSTRUCTION(loada);
INSTRUCTION(stora);    INSTRUCTION(tnset);




#endif


