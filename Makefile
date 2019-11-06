CC=clang
CFLAGS=-O3 -std=c99 -Wall
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

