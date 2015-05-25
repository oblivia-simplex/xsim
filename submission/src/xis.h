#ifndef XIS_H
#define XIS_H

#define XIS_VERSION 0x0100    /* Version, major 3, minor 2 */
#define XIS_TRAILER_SIZE 6    /* size of file trailer */

#define XIS_MEM_SIZE 65536    /* Size of the memory space for corewares */

#define XIS_REGS    16        /* Number of general registers */
#define XIS_OPS     0xC0      /* Number of operands mask */
#define XIS_1_IMED  0x20      /* Immediate mask for one operand instructions */
#define XIS_X_REG  0x20       /* Register mask for extended instructions */
#define XIS_EXTENDED 0x3      /* Extended instructions */
#define XIS_NUM_OPS(x)   (((x) >> 6 ) & XIS_EXTENDED)
#define XIS_IS_EXT_OP(x) (XIS_NUM_OPS(x) == XIS_EXTENDED)
#define XIS_REG1(x)      (((x) >> 4 ) & 0xf)
#define XIS_REG2(x)      ((x) & 0xf)
#define XIS_REL_SIZE 8        /* number of bits in a relative address */
#define XIS_ABS_SIZE 16       /* number of bits in an absolute address */

#define I_NUM 37              /* number of opcodes */

/* Instruction (opcodes) encodings */

enum {
 I_BAD     = 0x00, /* 00  000000 */

                   /* OPS  OPNUM    Operations with no operands */
 I_RET     = 0x01, /* 00  000001 */
 I_CLD     = 0x02, /* 00  000010 */
 I_STD     = 0x03, /* 00  000011 */

                   /* OPS I OPNUM   Operations with one register operand */
 I_NEG     = 0x41, /* 01  0 00001  */
 I_NOT     = 0x42, /* 01  0 00010  */
 I_PUSH    = 0x43, /* 01  0 00011  */
 I_POP     = 0x44, /* 01  0 00100  */
 I_JMPR    = 0x45, /* 01  0 00101  */
 I_CALLR   = 0x46, /* 01  0 00110  */
 I_OUT     = 0x47, /* 01  0 00111  */
 I_INC     = 0x48, /* 01  0 01000  */
 I_DEC     = 0x49, /* 01  0 01001  */

                   /* OPS I OPNUM   Operations with one immediate operand */
 I_BR      = 0x61, /* 01  1 00001  */
 I_JR      = 0x62, /* 01  1 00010  */

                   /* OPS  OPNUM   Operations with two register operands */
 I_ADD     = 0x81, /*  10  000001  */
 I_SUB     = 0x82, /*  10  000010  */
 I_MUL     = 0x83, /*  10  000011  */
 I_DIV     = 0x84, /*  10  000100  */
 I_AND     = 0x85, /*  10  000101  */
 I_OR      = 0x86, /*  10  000110  */
 I_XOR     = 0x87, /*  10  000111  */
 I_SHR     = 0x88, /*  10  001000  */
 I_SHL     = 0x89, /*  10  001001  */
 
 I_TEST    = 0x8A, /*  10  001010  */
 I_CMP     = 0x8B, /*  10  001011  */
 I_EQU     = 0x8C, /*  10  001100  */

 I_MOV     = 0x8D, /*  10  001101  */
 I_LOAD    = 0x8E, /*  10  001110  */
 I_STOR    = 0x8F, /*  10  001111  */
 I_LOADB   = 0x90, /*  10  010000  */
 I_STORB   = 0x91, /*  10  010001  */

                   /* OPS  R OPNUM   X Operations with one immediate operand */
 I_JMP     = 0xC1, /*  11  0 00001 */
 I_CALL    = 0xC2, /*  11  0 00010 */

                   /* OPS  R OPNUM   X Ops with 1 imm. and 1 reg. operand */
 I_LOADI   = 0xE1, /*  11  1 00001 */
};

struct x_inst { /* instruction mnemonic mapping struct */
  char *inst;
  int  code;
};


/* Instructioni-pneumonic mapping table */

#ifndef X_INSTRUCTIONS_NOT_NEEDED
#define X_INSTRUCTIONS_NOT_NEEDED
static struct x_inst x_instructions[I_NUM] = {
 { "ret", I_RET },
 { "cld", I_CLD },
 { "std", I_STD },

 { "neg", I_NEG },
 { "not", I_NOT },
 { "push", I_PUSH },
 { "pop", I_POP },
 { "jmpr", I_JMPR },
 { "callr", I_CALLR },
 { "out", I_OUT },
 { "inc", I_INC },
 { "dec", I_DEC },

 { "br", I_BR },
 { "jr", I_JR },

 { "add", I_ADD },
 { "sub", I_SUB },
 { "mul", I_MUL },
 { "div", I_DIV },
 { "and", I_AND },
 { "or", I_OR },
 { "xor", I_XOR },
 { "shr", I_SHR },
 { "shl", I_SHL },
 
 { "test", I_TEST },
 { "cmp", I_CMP },
 { "equ", I_EQU },

 { "mov", I_MOV },
 { "load", I_LOAD },
 { "stor", I_STOR },
 { "loadb", I_LOADB },
 { "storb", I_STORB },

 { "jmp", I_JMP },
 { "call", I_CALL },

 { "loadi", I_LOADI },

 { NULL,    0 },
};
#endif

#endif


