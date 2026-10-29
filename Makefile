# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# libsurface: the device behaviour of motorised control surfaces, as libsurface.a, and its
# optional DigiNet module (a Control|24 over libdiginet) as libsurface-diginet.a. See BUILDING.md.
#
# `make shared` and `make install-shared` are what the packages build: the same objects as shared
# libraries.

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
BINDIR  ?= $(PREFIX)/bin
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

# ---- the DigiNet module (libsurface-diginet) ------------------------------------------------
# Built when libdiginet with the desk mux (diginet/transport/diginet_mux.h, libdiginet 0.1.2 and
# later) is found: through pkg-config when it is installed, or from a built source tree with
#   make DIGINET_SRC=../libdiginet
# which is linked from its archives (for working on the two together, never for a package).
# Without it the core builds and tests alone, and `make` and `make test` say so on a SKIP line.
DIGINET_SRC ?=
ifneq ($(DIGINET_SRC),)
DIGINET_CFLAGS := -I$(DIGINET_SRC)/include
DIGINET_LIBS   := $(DIGINET_SRC)/libdiginet-transport.a $(DIGINET_SRC)/libdiginet.a
else
DIGINET_CFLAGS := $(shell pkg-config --cflags libdiginet-transport 2>/dev/null)
DIGINET_LIBS   := $(shell pkg-config --libs libdiginet-transport 2>/dev/null)
endif
HAVE_DIGINET := $(shell printf '\043include <diginet/transport/diginet_mux.h>\n' | \
	$(CC) $(DIGINET_CFLAGS) -E -x c - >/dev/null 2>&1 && echo yes)
DIGINET_NONE := libdiginet with the desk mux (diginet/transport/diginet_mux.h) was not found: \
	install libdiginet 0.1.2 or later, or set DIGINET_SRC to a built libdiginet tree

DOBJS := src/diginet/surface_diginet.o
DABI  := $(shell sed -n 's/^\#define SURFACE_DIGINET_ABI  *\([0-9]*\)$$/\1/p' include/surface/surface_diginet.h)

src/diginet/%.o: src/diginet/%.c
	$(CC) $(CFLAGS) $(WARN) -MMD -MP $(INC) $(DIGINET_CFLAGS) -c $< -o $@

-include $(DOBJS:.o=.d)

libsurface-diginet.a: $(DOBJS)
	$(AR) rcs $@ $(DOBJS)

ifeq ($(HAVE_DIGINET),yes)
diginet: libsurface-diginet.a
else
diginet:
	@echo "SKIP: the DigiNet module (libsurface-diginet) not built: $(DIGINET_NONE)"
endif

all: diginet

$(TESTS): tests/%: tests/%.c tests/harness.h libsurface.a
	$(CC) $(CFLAGS) $(WARN) $(INC) $< libsurface.a $(LIBS) -o $@

# ---- shared library (the packages) ---------------------------------------------------------
# --no-undefined: a symbol nothing defines fails here, not at a consumer's link.
SO := libsurface.so.$(VERSION)

$(SO): $(OBJS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libsurface.so.$(ABI) -Wl,--no-undefined -o $@ $(OBJS) $(LIBS)
	ln -sf $@ libsurface.so.$(ABI)
	ln -sf libsurface.so.$(ABI) libsurface.so

DSO := libsurface-diginet.so.$(VERSION)

$(DSO): $(DOBJS) $(SO)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libsurface-diginet.so.$(DABI) -Wl,--no-undefined -o $@ \
	    $(DOBJS) -L. -lsurface $(DIGINET_LIBS) $(LIBS)
	ln -sf $@ libsurface-diginet.so.$(DABI)
	ln -sf libsurface-diginet.so.$(DABI) libsurface-diginet.so

ifeq ($(HAVE_DIGINET),yes)
shared-diginet: $(DSO)
else
shared-diginet: diginet
endif

shared: $(SO) shared-diginet

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

# What both installs carry: the headers and the pkg-config files, the module's when it is built.
install-common:
	install -d $(DESTDIR)$(INCDIR)/surface $(DESTDIR)$(PCDIR)
	install -m 644 include/surface/surface.h $(DESTDIR)$(INCDIR)/surface/
	$(PCSED) packaging/libsurface.pc.in > $(DESTDIR)$(PCDIR)/libsurface.pc
ifeq ($(HAVE_DIGINET),yes)
	install -m 644 include/surface/surface_diginet.h $(DESTDIR)$(INCDIR)/surface/
	$(PCSED) packaging/libsurface-diginet.pc.in > $(DESTDIR)$(PCDIR)/libsurface-diginet.pc
endif

install: libsurface.a diginet install-common
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 libsurface.a $(DESTDIR)$(LIBDIR)/
ifeq ($(HAVE_DIGINET),yes)
	install -m 644 libsurface-diginet.a $(DESTDIR)$(LIBDIR)/
endif

# The packages: the shared libraries with their soname links.
install-shared: shared install-common
	install -d $(DESTDIR)$(LIBDIR)
	install -m 755 $(SO) $(DESTDIR)$(LIBDIR)/
	ln -sf $(SO) $(DESTDIR)$(LIBDIR)/libsurface.so.$(ABI)
	ln -sf libsurface.so.$(ABI) $(DESTDIR)$(LIBDIR)/libsurface.so
ifeq ($(HAVE_DIGINET),yes)
	install -m 755 $(DSO) $(DESTDIR)$(LIBDIR)/
	ln -sf $(DSO) $(DESTDIR)$(LIBDIR)/libsurface-diginet.so.$(DABI)
	ln -sf libsurface-diginet.so.$(DABI) $(DESTDIR)$(LIBDIR)/libsurface-diginet.so
endif

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) libsurface.a $(TESTS) libsurface.so*
	rm -f $(DOBJS) $(DOBJS:.o=.d) libsurface-diginet.a libsurface-diginet.so*
	rm -rf build

.PHONY: all diginet test shared shared-diginet install install-common install-shared clean
