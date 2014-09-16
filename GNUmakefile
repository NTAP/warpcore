OS=$(shell uname -s)

CC=cc
COPT=-Ofast -march=native
CDEB=-g -pg -ftrapv -DDLEVEL=10
# CDEB+=-DNDEBUG
CDIA=-Wall -Wextra -fdiagnostics-color=auto

ifeq ($(OS), Linux)
CDEF+=-D_GNU_SOURCE
CINC+=-isystem ~/netmap/sys
else
CDIA+=-pedantic -Weverything -Wno-gnu-zero-variadic-macro-arguments
CDIA+=-Wno-padded -Wno-packed -Wno-missing-prototypes -Wno-cast-align -Wno-conversion
endif

CFLAGS+=-pipe -std=c11 $(COPT) $(CDEB) $(CDIA) $(CDEF) $(CINC)

# see http://bruno.defraine.net/techtips/makefile-auto-dependencies-with-gcc/
OUTPUT_OPTION=-MMD -MP -o $@

LDLIBS=-pthread
LDFLAGS=$(CFLAGS)

CSRC=warptest.c warpinetd.c warpping.c
COBJ=$(addprefix $(OS)/, $(CSRC:.c=.o))
CMD=$(COBJ:.o=)

LSRC=$(filter-out $(CSRC).c, $(wildcard *.c))
LOBJ=$(addprefix $(OS)/, $(LSRC:.c=.o))

DEP=$(CSRC:.c=.d) $(LSRC:.c=.d)

$(OS)/%.o: %.c
	$(CC) $(CFLAGS) -c $(OUTPUT_OPTION) $<

all: $(OS) $(CMD)

$(CMD): $(OS)/libwarpcore.a $@

$(OS):
	mkdir -p $(OS)

$(OS)/libwarpcore.a: $(LOBJ)
	ar -crs $@ $^


.PHONY: clean lint

clean:
	-@rm -r $(OS) *core *.gmon 2> /dev/null || true

lint:
	cppcheck -D1 --enable=all *.c --check-config -I /usr/include

-include $(DEP)
