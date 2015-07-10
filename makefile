# Targets & general dependencies
PROGRAM = xmpsim
HEADERS = xis.h xcpu.h xdb.h 
OBJS = xcpu.o xmpsim.o xdb.o
DUMPOBJ = xcpu.o xdb.o xdump.o 
ADD_OBJS = 
GOLD = xmpsim_gold 

# compilers, linkers, utilities, and flags
CC = gcc
CFLAGS = -Wall -g
COMPILE = $(CC) $(CFLAGS)
LINK = $(CC) $(CFLAGS) -o $@ 

# implicit rule to build .o from .c files
%.o: %.c $(HEADERS)
	$(COMPILE) -c -o $@ $<


# explicit rules
all: xld xas xcc xmkos $(GOLD) xmpsim 

$(PROGRAM): $(OBJS) $(ADD_OBJS)
	$(LINK) $(OBJS) $(ADD_OBJS) -l pthread

xdump: $(DUMPOBJ)
	$(LINK) $(DUMPOBJ) 


xmpsim_gold: libxmpsim.a 
	$(LINK) libxmpsim.a -l pthread

xas: xas.o xreloc.o
	$(LINK) xas.o xreloc.o

xld: xld.o xreloc.o
	$(LINK) xld.o xreloc.o

xmkos: xmkos.o xreloc.o
	$(LINK) xmkos.o xreloc.o

xcc: xcc.o 
	$(LINK) xcc.o

lib: xmpsim_gold.o xcpu_gold.o
	 ar -r libxmpsim.a xmpsim_gold.o xcpu_gold.o 

clean:
	rm -f *.o *.xo *.xx $(PROGRAM) xdump xas xld xcc xmkos $(GOLD)

zip:
	make clean
	rm -f xmpsim.zip
	zip xmpsim.zip *
