OS=$(shell uname -s)

CC=cc
# CDEF=-DNDEBUG
COPT=-O0
CDEB=-g -pg -ftrapv -march=native
CDIA=-Wall -Wextra -fdiagnostics-color=auto
ifeq ($(OS), Linux)
CINC+=-isystem ~/netmap/sys
CDIA+=-Wformat=0
else
#CDIA+=-Weverything -pedantic
endif
CFLAGS+=-pipe -std=c99 $(COPT) $(CDEB) $(CDIA) $(CDEF) $(CINC)

# see http://bruno.defraine.net/techtips/makefile-auto-dependencies-with-gcc/
OUTPUT_OPTION=-MMD -MP -o $@

LDLIBS=-pthread

lib=warpcore
cmd=test

SRC=$(filter-out $(cmd).c, $(wildcard *.c))
OBJ=$(SRC:.c=.o)
DEP=$(SRC:.c=.d)

all: $(cmd)
$(cmd): lib$(lib).a $(cmd).o

lib$(lib).a: $(OBJ)
	ar -crs $@ $^

.PHONY: clean distclean lint

clean:
	-@rm $(cmd) $(cmd).o $(cmd).d $(cmd).core \
		lib$(lib).a $(OBJ) 2> /dev/null || true

distclean: clean
	-@rm $(DEP) 2> /dev/null || true

lint:
	cppcheck -D1 --enable=all *.c --check-config -I /usr/include

-include $(DEP)
