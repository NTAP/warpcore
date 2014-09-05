CC=cc
COPT=-O0
CDEB=-g -pg -ftrapv -march=native
CDIA=-Wall -Wextra -fdiagnostics-color=auto
CFLAGS+=-pipe -std=c99 $(COPT) $(CDEB) $(CDIA)

# see http://bruno.defraine.net/techtips/makefile-auto-dependencies-with-gcc/
OUTPUT_OPTION=-MMD -MP -o $@

LDFLAGS+=-Wl --warn-common
LDLIBS=-lthr

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
