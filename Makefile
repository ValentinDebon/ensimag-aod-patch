CC=clang
CFLAGS=-g -std=c99 -Wall -DPATCH_COSTS_PRINT
LDFLAGS=

COMPUTEPATCHOPT=bin/computePatchOpt

.PHONY: all clean doc

all: $(COMPUTEPATCHOPT)

clean:
	rm -rf bin doc

doc:
	doxygen

bin:
	mkdir $@

bin/%: src/%.c bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

