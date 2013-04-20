prefix=/usr/local

CC=gcc
CCFLAGS=-pg -g -O2 -lgit2 -W -Wall -pedantic -MMD

all: build

build: mkbuilddir build/git-recent

install: build/git-recent
	install -m 0755 build/git-recent $(prefix)/bin

mkbuilddir:
	mkdir -p build

.PHONY: mkbuilddir

build/git-recent.o: src/git-recent.c
	$(CC) $(CCFLAGS) -c -o build/git-recent.o src/git-recent.c

build/tracked.o: src/tracked.c
	$(CC) $(CCFLAGS) -c -o build/tracked.o src/tracked.c

build/git-recent: build/git-recent.o build/tracked.o
	$(CC) $(CCFLAGS) -o build/git-recent build/git-recent.o build/tracked.o
