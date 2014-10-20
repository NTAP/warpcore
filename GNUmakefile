OS=$(shell uname -s | tr '[:upper:]' '[:lower:]')

# parallelize the build
ifeq ($(OS), linux)
NPROCS:=$(shell grep -c ^processor /proc/cpuinfo)
else
NPROCS:=$(shell sysctl -n hw.ncpu)
endif
MAKEFLAGS+="-j -l $(NPROCS)"

CC=cc

# CFLAGS for all compilers
CFLAGS+=-pipe -std=c11 -g
CFLAGS+=-Ofast -march=native -fno-strict-aliasing
CFLAGS+=-Wall -Wextra -fdiagnostics-color=auto
CFLAGS+=-DDLEVEL=debug
# CFLAGS+=-DNDEBUG
# CFLAGS+=-pg -ftrapv

# additional CFLAGS that are compiler-specific
ifeq ($(CC), gcc49)
# CFLAGS+=-finline-limit=65535
CFLAGS+=-Winline
else
CFLAGS+=-Wno-gnu-zero-variadic-macro-arguments -Wno-padded -Wno-packed
CFLAGS+=-Wno-cast-align
endif

# additional CFLAGS that are platform-specific
ifeq ($(OS), linux)
CFLAGS+=-D_GNU_SOURCE -isystem ~/netmap/sys
else
CFLAGS+=-pedantic -Weverything
endif

# see http://bruno.defraine.net/techtips/makefile-auto-dependencies-with-gcc/
OUTPUT_OPTION=-MMD -MP -o $@

# LDLIBS=-pthread
LDFLAGS=$(CFLAGS)

CSRC=warpinetd.c warpping.c
COBJ=$(addprefix $(OS)/, $(CSRC:.c=.o))
CMD=$(COBJ:.o=)

LSRC=$(filter-out $(CSRC), $(wildcard *.c))
LSRC:=$(filter-out $(wildcard plat_*.c), $(LSRC))
LSRC+=plat_$(OS).c
LOBJ=$(addprefix $(OS)/, $(LSRC:.c=.o))

DEP=$(COBJ:.o=.d) $(LOBJ:.o=.d)

all: $(OS) $(CMD)

$(CSRC) $(LSRC) $(wildcard *.h): GNUmakefile

$(OS)/%.o: %.c
	$(CC) $(CFLAGS) -c $(OUTPUT_OPTION) $<

$(CMD): $(LOBJ) $@
#$(CMD): $(OS)/libwarpcore.a $@

$(OS):
	mkdir -p $(OS)

$(OS)/libwarpcore.a: $(LOBJ)
	ar -crs $@ $^


.PHONY: clean lint

clean:
	-@rm -r $(OS) *core vgcore.* *.gmon gmon.out 2> /dev/null || true

lint:
	cppcheck -D1 --enable=all *.c --check-config -I /usr/include

-include $(DEP)
