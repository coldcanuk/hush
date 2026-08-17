# Makefile for Hush C (legible C11)
CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Iinclude -O2
LDFLAGS :=

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)
TESTS := $(wildcard tests/test_*.c)
TEST_BINS := $(TESTS:.c=)

.PHONY: all test clean demo

all: libhush.a hush-relay

libhush.a: $(OBJS)
	ar rcs $@ $^

hush-relay: src/hush_relay_main.o libhush.a
	$(CC) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_%: tests/test_%.c libhush.a
	$(CC) $(CFLAGS) $< libhush.a -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "RUN $$t"; ./$$t || exit 1; done
	@echo "ALL TESTS PASSED"

clean:
	rm -f $(OBJS) libhush.a hush-relay $(TEST_BINS) src/hush_relay_main.o

demo:
	@echo "See hush-c/demo/ for Tailwind UI demo (static)"
