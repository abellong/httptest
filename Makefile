# Makefile for httptest

BINDIR =	/usr/local/bin
MANDIR =	/usr/local/man/man1
CC =		gcc -Wall -g
CFLAGS =	$(SRANDOM_DEFS)
LDFLAGS =	$(SYSV_LIBS)

all:		httptest

httptest:	httptest.o timers.o
	$(CC)   $(CFLAGS) httptest.o timers.o $(LDFLAGS) -o httptest -lpanel -lncurses

httptest.o:	httptest.c timers.h
	$(CC)   $(CFLAGS) -c httptest.c

timers.o:	timers.c timers.h
	$(CC)   $(CFLAGS) -c timers.c

install:	all
	rm -f $(BINDIR)/httptest
	cp httptest $(BINDIR)
	rm -f $(MANDIR)/httptest.1
	cp httptest.1 $(MANDIR)

clean:
	rm -f httptest *.o core core.* *.core
