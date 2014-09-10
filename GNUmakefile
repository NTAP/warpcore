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
#CDIA+=-Weverything -pedantic -ferror-limit=1000 -Wno-gnu-zero-variadic-macro-arguments -Wno-packed -Wno-padded -Wno-missing-prototypes -Wno-cast-align -Wno-conversion
endif

CFLAGS+=-pipe -std=c99 $(COPT) $(CDEB) $(CDIA) $(CDEF) $(CINC)

# see http://bruno.defraine.net/techtips/makefile-auto-dependencies-with-gcc/
OUTPUT_OPTION=-MMD -MP -o $@

LDLIBS=-pthread
LDFLAGS=$(CFLAGS)

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
	-@rm $(cmd) $(cmd).o $(cmd).d $(cmd).core $(cmd).gmon \
		lib$(lib).a $(OBJ) 2> /dev/null || true

distclean: clean
	-@rm $(DEP) 2> /dev/null || true

lint:
	cppcheck -D1 --enable=all *.c --check-config -I /usr/include

-include $(DEP)
