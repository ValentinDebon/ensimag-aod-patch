CC=clang
CFLAGS=-O3 -std=c99 -Wall
LDFLAGS=

COMPUTEPATCHOPT=bin/computePatchOpt

.PHONY: all clean test

all: $(COMPUTEPATCHOPT)

clean:
	rm -rf bin

test: $(COMPUTEPATCHOPT)
	$(COMPUTEPATCHOPT) test/computePatchOpt/source test/computePatchOpt/destination patch

bin:
	mkdir $@

bin/%: src/%.c bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

