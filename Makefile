PREFIX=/usr/local

CC=gcc
CFLAGS=-g -O2 -lgit2 -W -Wall -pedantic

all: prepare-build git-recent

prepare-build:
	mkdir -p obj

.PHONY: prepare-build

git-recent: obj/git-recent.o obj/tracked.o
	$(CC) $(CFLAGS) -o $@ $^

obj/git-recent.o: src/git-recent.c src/tracked.h
	$(CC) $(CFLAGS) -c -o $@ $(firstword $^)

obj/tracked.o: src/tracked.c src/tracked.h
	$(CC) $(CFLAGS) -c -o $@ $(firstword $^)

install: build/git-recent
	install -m 0755 git-recent ${PREFIX}/bin
