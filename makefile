# Makefile for hw4

all: test

test:
	gcc getaddrinfo.c -std=c99 -Wall

clean:
	rm a.out
