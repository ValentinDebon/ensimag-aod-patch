CC=clang++
CFLAGS=-g -Wall -std=c++11
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

bin/%: src/%.cc bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

