=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
|                       XCPU IMPLEMENTATION                       |
|                   WITH STDIO LIBRARY AND TESTS                  |
|                      BY OLIVIA LUCCA FRASER                     |
|                            B00109376                            |
=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

To run, just compile xsim.c with the command gcc -o xsim xsim.c, but
be sure that the supplied versions of xcpu.h are xcpu.c in the same
working directory. The emulator is then run with the command

./xsim <number of cycles> <.x or .xo object file compiled by xcc or xas>

THE DEBUGGER
=-=-=-=-=-=-

This will generate a fair bit of output to stderr, which can be
easily dispensed with by redirecting to a log file, or to /dev/null.
This is the output of the extended debugger, which displays, for each
cycle:

(a) the current contents of the registers and PC counter, as printed
    by the standard debugger;
(b) the number of the cycle;
(c) a disassembly of the instruction that has just been executed, usually
    with a small amount of English commentary;
(d) the top 30 addresses of the stack, in both hex and ascii form, and
(e) the top 30 addresses of the code segment of memory, in both hex and
    ascii form.

This debugger can be permanently switched off by setting the MOREDEBUG
macro flag, defined at the head of xcpu.c, to 0. Since MOREDEBUG pipes its
output to stderr (or to whatever file descriptor is set as the value of
the LOG macro, defined just after the MOREDEBUG flag), it can be left
active without interfering with the output triggered by the debug bit
(which flows to stdout, by default). Setting MOREDEBUG to 0, however,
can significantly increase performance, if speed is desired. (Though
information is probably of more use than speed, for an experimental
setup like this one.)

The implementation of the extended debugger is mostly contained inside
the function xcpu_pretty_print, which is called from xcpu_execute if
MOREDEBUG is set. The exception is the disassembly of the active inst-
ruction, which is performed inside each instruction function. There was
a tradeoff, here, in designing the debugger, between tidy modularity on
the one hand, and keeping the debugging information close to the mecha-
nisms being debugged, on the other. A more elegant solution would be
worth pursuing, however.

THE INSTRUCTION SET
=-=-=-=-=-=-=-=-=-=

This implementation of xcpu packages each machine instruction in a
separate function. As soom as xsim is executed, it allocates an xcpu_
context structure to heap memory, loads the object file into the memory
array of the xcpu_context (the virtual machine), and then allocates a
jump table of function pointers, containing pointers to each machine
instruction, to the heap. It then begins the execution cycle, iteratively
calling xcpu_execute(), which is implemented in xcpu.c. This function takes
two parameters: a pointer to the xcpu_context structure, and a pointer to
the instruction-set jump table.

Inside xcpu_execute, the machine instruction at the memory address indexed
by the programme counter is read, and its first byte is extracted. This is
the opcode of the instruction, and is an index into the instruction-set
jump table. (For instance, the opcode of the instruction XOR is 0x87, and
so table[0x87] is a pointer to the function that implements XOR -- a func-
tion conveniently named 'xor()'.) Any cell in the jump table array that
isn't indexed by a valid opcode has already been initialized to point to
bad() -- i.e., to be equivalent to the opcode 0x00. This helps prevent
undefined behaviour and segmentation faults, in the event of corrupted
object files.

THE TESTS
=-=-=-=-=

To test this virtual machine, a battery of short programmes has been
written in the xas assembly language. Together, they thoroughly cover
the instruction set used by the machine. They range from unremarkable
series of instructions, with no significant global structure, to short
programmes designed to perform some (moderately interesting) task.
(Test 9, for example, checks strings to see if they're palindromes.
Test 10 encrypts a string with a Caesar cipher, decrypts it again, and
then encrypts and decrypts it again using a very simple version of
symmetric key encryption -- xorring the string against another string,
stored in memory.) When the test programme itself doesn't make heavy
use of stdout (as it does in the last two mentioned), the std flag is
set, so that the contents of the registers during execution can be
compared against their contents when the same file is executed by
xsim_gold.

To repeat the tests, first compile them with the command

./xcc -o test.#.x test.#.xas stdio.xas xrt0.xas

STDIO
=-=-=

The stdio.xas file used for this assignment can be found in the lib
subdirectory. Several of the tests make use of functions defined in
this file, and so, as a rule of thumb, it's best to link stdio.xo
when compiling the test code. Functions defined in stdio.xas include

puts    (which was already there)
memcpy  (as required by the assignment)
mapbyte (an implementation of map() for byte arrays)
border  (a purely ornamental function, which draws nice horizontal borders)
strlen  (an implementation of the C function strlen)
