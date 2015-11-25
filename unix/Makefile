CC=clang -std=gnu11
AS=clang -integrated-as -mllvm --x86-asm-syntax=intel -x assembler-with-cpp
CFLAGS=-Os -g
AFLAGS=
PREFIX=/usr/local
LIBDIR=$(PREFIX)/lib
ARCH=$(shell uname -m)

LIB=libmultitask.a
HEADER=multitask.h

SOURCES=\
	../chan.c\
	../iochan.c\
	../lock.c\
	../qlock.c\
	../queue.c\
	../ref.c\
	../rendez.c\
	../task.c\
	../task-$(ARCH).s\
	../timechan.c\
	../timequeue.c\

TMP=$(SOURCES:.c=.o)

OBJECTS=$(TMP:.s=.o)

HEADERS=\
	../multitask.h\
	../multitask-impl.h\
	u.h\
	libc.h\

all: $(LIB) $(HEADER)

$(SOURCES): $(HEADERS)

.s.o:
	$(AS) $(AFLAGS) -include assembly.h -c -o $@ $<

.c.o:
	$(CC) $(CFLAGS) -I. -c -o $@ $<

$(HEADER): $(HEADER).in ../$(HEADER)
	./makehdr.sh $(HEADER) > $(HEADER)

$(LIB): $(OBJECTS)
	ar rusc $@ $(OBJECTS)

install: $(LIB) $(HEADER)
	install -D -m 644 $(HEADER) "$(DESTDIR)/$(PREFIX)/include/$(HEADER)"
	install -D -m 644 $(LIB) "$(DESTDIR)/$(LIBDIR)/$(LIB)"

clean:
	rm -f $(OBJECTS) $(LIB) $(HEADER)
