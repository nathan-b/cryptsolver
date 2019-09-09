LIBS=-lncurses

CFLAGS=-Wall -W -pedantic -std=c99
DCFLAGS=-g -DDEBUG
NCFLAGS=-O3

OUTFILE=csolver

INFILES=cryptsolver.c

nondebug:
	gcc $(CFLAGS) $(NCFLAGS) $(LIBS) -o $(OUTFILE) $(INFILES)

debug:
	gcc $(CFLAGS) $(DCFLAGS) $(LIBS) -o $(OUTFILE) $(INFILES)
    
clean:
	rm $(OUTFILE)

