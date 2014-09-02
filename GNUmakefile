CFLAGS=-pipe -std=c99 -O0 -g -pg -ftrapv -Wall -Wextra

SRCS=debug.c eth.c ip.c icmp.c in_cksum.c
OBJS=$(SRCS:.c=.o)
DEPS=$(SRCS:.c=.d)

lib=warpcore
cmd=test

all: $(cmd)
$(cmd): lib$(lib).a $(cmd).o

lib$(lib).a: $(OBJS)
	ar -crs $@ $^

.PHONY: clean distclean lint

clean:
	-@rm $(cmd) $(cmd).o lib$(lib).a $(cmd).core $(OBJS) 2> /dev/null || true

distclean:
	-@rm $(DEPS) 2> /dev/null || true

lint:
	cppcheck -D1 --enable=all *.c --check-config -I /usr/include

# advanced auto-dependency generation; see
# http://mad-scientist.net/make/autodep.html
%.o : %.c
	$(COMPILE.c) -MD -o $@ $<
	@sed -e 's/#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' \
		-e '/^$$/ d' -e 's/$$/ :/' -i '' $*.d

-include $(DEPS)
