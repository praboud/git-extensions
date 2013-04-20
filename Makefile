prefix=/usr/local

all: build

build: mkbuilddir git-recent

install: build/git-recent
	install -m 0755 build/git-recent $(prefix)/bin

mkbuilddir:
	mkdir -p build

.PHONY: mkbuilddir

git-recent: src/gitext.c
	gcc -lgit2 -o build/git-recent src/gitext.c
