#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX frontends (dynamically loaded)
#
# Copyright (C) 1999, Mark Spencer
#
# Mark Spencer <markster@linux-support.net>
#
# $Id: Makefile,v 1.20 2004/07/15 18:25:38 peternixon Exp $
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

#
# Set ASTERISKINCDIR variable to the directory containing the sources of
# Asterisk PBX.
#
ASTERISKINCDIR=/usr/include/asterisk

#
# Set ASTERISKMODDIR variable to the directory where ASTERISK's modules reside.
# The modules will be installed in this directory.
#
ASTERISKMODDIR=/usr/lib/asterisk/modules

#
# Set ASTERISKETCDIR variable to the directory where ASTERISK's configuration
# files reside. The config files will be installed in this directory.
#
ASTERISKETCDIR=/etc/asterisk

# Install the Least Cost Dial App
APPS=app_lcdial.so

CC=gcc
INSTALL=install

#Tell gcc to optimize the asterisk's code
OPTIMIZE=-O6

#Include debug symbols in the executables (-g) and profiling info (-pg)
DEBUG=-g #-pg

INCLUDE=-Iinclude -I../include
CFLAGS=-pipe  -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations $(DEBUG) $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE #-DMAKE_VALGRIND_HAPPY
CFLAGS+=$(OPTIMIZE)
CFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
CFLAGS+=$(shell if uname -m | grep -q ppc; then echo "-fsigned-char"; fi)
CFLAGS+=$(shell if [ -f /usr/include/osp/osp.h ]; then echo "-DOSP_SUPPORT -I/usr/include/osp" ; fi)

INCLUDE+= ${shell mysql_config --cflags}
LIBS+= ${shell mysql_config --libs}


#
# Uncomment this line, if you are compiling against CVS soure
#
CFLAGS+=-DUSE_CVS

ifeq (${OSARCH},FreeBSD)
OSVERSION=$(shell make -V OSVERSION -f /usr/share/mk/bsd.port.subdir.mk)
CFLAGS+=$(if ${OSVERSION}<500016,-D_THREAD_SAFE)
LIBS+=$(if ${OSVERSION}<502102,-lc_r,-pthread)
INCLUDE+=-I/usr/local/include
CFLAGS+=$(shell if [ -d /usr/local/include/spandsp ]; then echo "-I/usr/local/include/spandsp"; fi)
endif # FreeBSD

ifeq (${OSARCH},OpenBSD)
CFLAGS+=-pthread
endif

#Uncomment this to use the older DSP routines
#CFLAGS+=-DOLD_DSP_ROUTINES

CFLAGS+=$(shell if [ -f /usr/include/linux/zaptel.h ]; then echo "-DZAPTEL_OPTIMIZATIONS"; fi)
CFLAGS+=$(shell if [ -f /usr/local/include/zaptel.h ]; then echo "-DZAPTEL_OPTIMIZATIONS"; fi)

LIBEDIT=editline/libedit.a

CFLAGS+= $(DEBUG_THREADS)
CFLAGS+= $(TRACE_FRAMES)
CFLAGS+= $(MALLOC_DEBUG)
CFLAGS+= $(BUSYDETECT)
CFLAGS+= $(OPTIONS)

CFLAGS+=-fPIC
#CFLAGS+=-fPIC -lpthread -ldl -lncurses -lm

all: $(APPS)

clean:
	rm -rf *.so *.o *~ look .depend 
	rm -rf doc/*~

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

install: all
	mkdir -p $(DESTDIR)$(ASTERISKMODDIR)
	for x in $(APPS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(ASTERISKMODDIR) ; done

app_lcdial.o: app_lcdial.c
	$(CC) -pipe $(ASTERISKINCLUDE) $(CFLAGS) -c -o app_lcdial.o app_lcdial.c

app_lcdial.so: app_lcdial.o
	$(CC) -shared -Xlinker -x -o $@ $< $(LIBS)

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`

env:
	env
