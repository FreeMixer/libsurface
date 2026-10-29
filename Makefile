# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# libsurface: the device behaviour of motorised control surfaces, as libsurface.a. See
# BUILDING.md.
#
# `make shared` and `make install-shared` are what the packages build: the same objects as a
# shared library.

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -g
LDFLAGS ?=
# -fPIC always: the same objects go into the archive and the shared library.
WARN    := -std=c11 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -fPIC
INC     := -Iinclude
LIBS    := -lm
PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib
INCDIR  ?= $(PREFIX)/include
PCDIR   ?= $(LIBDIR)/pkgconfig
DESTDIR ?=

# The version is the VERSION file; the soname's major is in surface.h. The header's version
# string and the packaging copies are held to them by tests/conformance-packaging.sh.
VERSION := $(shell cat VERSION)
ABI     := $(shell sed -n 's/^\#define SURFACE_ABI  *\([0-9]*\)$$/\1/p' include/surface/surface.h)

OBJS := src/surface.o src/motor.o src/bank.o src/meter.o

TESTS := tests/test_motor tests/test_audit tests/test_bank tests/test_meter tests/test_c24

GOLDEN := tests/golden/meter-steps.txt
RUN_TEST := tests/run-test.sh

all: libsurface.a

# -MMD -MP: a header edit rebuilds every object that includes it.
src/%.o: src/%.c
	$(CC) $(CFLAGS) $(WARN) -MMD -MP $(INC) -c $< -o $@

-include $(OBJS:.o=.d)

libsurface.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(TESTS): tests/%: tests/%.c tests/harness.h libsurface.a
	$(CC) $(CFLAGS) $(WARN) $(INC) $< libsurface.a $(LIBS) -o $@

# ---- shared library (the packages) ---------------------------------------------------------
# --no-undefined: a symbol nothing defines fails here, not at a consumer's link.
SO := libsurface.so.$(VERSION)

$(SO): $(OBJS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libsurface.so.$(ABI) -Wl,--no-undefined -o $@ $(OBJS) $(LIBS)
	ln -sf $@ libsurface.so.$(ABI)
	ln -sf libsurface.so.$(ABI) libsurface.so

shared: $(SO)

test: $(TESTS)
	$(RUN_TEST) tests/test_motor
	$(RUN_TEST) tests/test_audit
	$(RUN_TEST) tests/test_bank
	$(RUN_TEST) tests/test_meter $(GOLDEN)
	$(RUN_TEST) tests/test_c24
	$(RUN_TEST) tests/constants-check.sh
	$(RUN_TEST) tests/test_install.sh
	$(RUN_TEST) tests/conformance-packaging.sh
	$(RUN_TEST) tests/test_workflows.sh
	$(RUN_TEST) tests/public-sources-check.sh
	$(RUN_TEST) tests/test_public_sources.sh

# The pkg-config file is written at install time, from the install's own PREFIX and LIBDIR: a
# file kept from an earlier build would name that build's directories.
PCSED = sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@LIBDIR@|$(LIBDIR)|' -e 's|@INCDIR@|$(INCDIR)|' \
	    -e 's|@VERSION@|$(VERSION)|'

# What both installs carry: the header and the pkg-config file.
install-common:
	install -d $(DESTDIR)$(INCDIR)/surface $(DESTDIR)$(PCDIR)
	install -m 644 include/surface/*.h $(DESTDIR)$(INCDIR)/surface/
	$(PCSED) packaging/libsurface.pc.in > $(DESTDIR)$(PCDIR)/libsurface.pc

install: libsurface.a install-common
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 libsurface.a $(DESTDIR)$(LIBDIR)/

# The packages: the shared library with its soname links.
install-shared: shared install-common
	install -d $(DESTDIR)$(LIBDIR)
	install -m 755 $(SO) $(DESTDIR)$(LIBDIR)/
	ln -sf $(SO) $(DESTDIR)$(LIBDIR)/libsurface.so.$(ABI)
	ln -sf libsurface.so.$(ABI) $(DESTDIR)$(LIBDIR)/libsurface.so

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) libsurface.a $(TESTS) libsurface.so*
	rm -rf build

.PHONY: all test shared install install-common install-shared clean
