CC=clang
CFLAGS=-g -std=c99 -Wall -DPATCH_COSTS_PRINT
LDFLAGS=

COMPUTEPATCHOPT=bin/computePatchOpt

.PHONY: all clean test doc

all: $(COMPUTEPATCHOPT)

clean:
	rm -rf bin doc

test: $(COMPUTEPATCHOPT)
	$< ./test/computePatchOpt/source ./test/computePatchOpt/destination patch

doc:
	doxygen

bin:
	mkdir $@

bin/%: src/%.c bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

