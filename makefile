CC ?= gcc
AR ?= ar
CFLAGS := -std=c99 -g -Wall -Wno-unused -O3 -fpic
ECFLAGS = -std=c99 -O3 -g -I.
DIR = ./bin
TEST = ./test
OBJECT = gc.o
STATIC = libgc.a
DYNAMIC = libgc.so


all: $(STATIC) $(DYNAMIC)
$(STATIC): $(OBJECT)
	$(AR) -crv $@ $^
$(DYNAMIC): $(OBJECT)
	$(CC) -shared -o $@ $^
$(OBJECT): gc.c gc.h
	$(CC) -c $(CFLAGS) gc.c

.PHONY: test
test: $(OBJECT)
	$(CC) $(ECFLAGS) $(TEST)/main.c $^ -o $(DIR)/main

.PHONY: clean
clean:
	rm -rf $(STATIC) $(DYNAMIC) $(OBJECT)
