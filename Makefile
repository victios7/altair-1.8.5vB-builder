# Altair Compiler v1.8.5 - Makefile
CC      = gcc
CFLAGS  = -O3 -flto -fomit-frame-pointer -Wall -Wextra -std=c11 \
          -Wno-unused-parameter \
          -DENABLE_FNUMLIST -DENABLE_SB \
          -DALTAIR_RT_H=\"runtime/altair_rt.h\" \
          -DALTAIR_RT_C=\"runtime/altair_rt.c\"

# Solo añadir -Wno-stringop-truncation si es GCC
ifeq ($(shell $(CC) -dumpversion >/dev/null 2>&1 && echo gcc),gcc)
  CFLAGS += -Wno-stringop-truncation
endif

SRCS = src/main.c src/lexer.c src/ast.c src/parser.c src/sema.c src/codegen.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean test install

all: altairc

altairc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm -flto
	@echo ""
	@echo "  altairc 1.8.5 built. Usage: ./altairc <program.at> -o <output>"
	@echo "  Guide: ./altairc guide"
	@echo ""

%.o: %.c
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

clean:
	rm -f $(OBJS) altairc

test: altairc
	./altairc examples/hello.at -o /tmp/altair_hello && /tmp/altair_hello

install: altairc
	@mkdir -p ~/.local/bin
	cp altairc ~/.local/bin/altairc
	@echo "Installed to ~/.local/bin/altairc"
