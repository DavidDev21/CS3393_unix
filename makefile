# Makefile for hw4

all: test

test:
	gcc test.c -std=c99 -Wall

clean:
	rm a.out
