# SPDX-License-Identifier: GPL-3.0-or-later
# libsurface, the device behaviour of motorised control surfaces: the motor-fader policy, touch,
# the bank window, the board shadow and its replay, meter steps and ring rendering.
Name:           libsurface
Version:        0.1.0
# The soname's major. rpm derives this package's provides and every consumer's requires from it.
# It copies SURFACE_ABI of include/surface/surface.h, and tests/conformance-packaging.sh (in make
# test) refuses a copy that disagrees.
%global abi 0
Release:        1%{?dist}
Summary:        Motor faders, touch and banks for any motorised control surface

License:        GPL-3.0-or-later
URL:            https://github.com/FreeMixer/libsurface
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  git-core
BuildRequires:  pkgconf-pkg-config

%description
libsurface is the device behaviour of a motorised control surface, written once in C for every
surface and every host: a motor-fader policy that never fights the hand, touch with a self-heal
for a lost release and a debounce for a chattering sensor, the bank window, a shadow of the
whole board replayed when a device comes back blank, meter steps with hysteresis, LED ring
rendering, and the health lists of faders that cannot be trusted. It speaks no wire and knows no
mixing semantics: a backend encodes, a host names what each strip shows.

%package devel
Summary:        Development files for libsurface
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
The header and the pkg-config file (libsurface) for building a control-surface host or backend
on libsurface.

%prep
%autosetup -n %{name}-%{version}

%build
%make_build CFLAGS="%{optflags}" LDFLAGS="%{build_ldflags}" shared

%install
# not %%make_install: it names the `install` target (the static archive) as well
make DESTDIR=%{buildroot} PREFIX=%{_prefix} LIBDIR=%{_libdir} INCDIR=%{_includedir} install-shared

%check
make CFLAGS="%{optflags}" test

%files
%license LICENSE
%doc README.md
%{_libdir}/libsurface.so.%{version}
%{_libdir}/libsurface.so.%{abi}

%files devel
%doc BUILDING.md
%dir %{_includedir}/surface
%{_includedir}/surface/*.h
%{_libdir}/libsurface.so
%{_libdir}/pkgconfig/libsurface.pc

%changelog
* Thu Oct 29 2026 Pau Aliagas <linuxnow@gmail.com> - 0.1.0-1
- First release: the device behaviour core, ported from a TypeScript
  X-Touch driver with its tests. The motor-fader policy (touch flag plus
  target, an implicit-touch window, the echo guard, a trailing coalesce),
  touch with a self-heal and a release debounce, the bank window, the
  board shadow and its replay, meter steps, ring rendering, and the
  do-not-drive, do-not-read, jump-filter and quarantine rules for the
  Control|24's tired faders. Caller time, no threads.
