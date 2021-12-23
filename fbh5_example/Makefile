# --- macros
CC= gcc
CFLAGS= -c -Wall -Werror -fpic
LFLAGS= -Wall 
OBJECTS= simon.o

# FBH5 include directory:
INCRAWSPEC= ../fbh5/

# FBH5 library directory:
FBH5_DIR= ../lib
FBH5_SO= :librawspec.so

# --- All targets.
all:	simon

# --- Test program executables.
simon:	simon.o $(OBJECTS)
	$(CC) $(LFLAGS) -o simon simon.o -L $(FBH5_DIR) -l $(FBH5_SO) -lm

# --- Remove binaries and data files in testdata subdirectory.
clean:
	rm -f simon $(OBJECTS)

# --- Store important suffixes in the .SUFFIXES macro.
.SUFFIXES:	.o .c	

# --- Generate anyfile.o from anyfile.c.
%.o:    %.c Makefile $(INCRAWSPEC)
	$(CC) $(CFLAGS) $< -I. -I $(INCRAWSPEC)

