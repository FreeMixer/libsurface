# Building libsurface

A hand-kept Makefile, no build system to configure. You need a C11 compiler and `make`.
Nothing else: no library outside libc and libm.

```
make                          # libsurface.a
make test                     # the unit suite, the same command CI runs
make shared                   # libsurface.so, as the packages build it
make install PREFIX=/usr/local [DESTDIR=...]
make clean
```

The code is plain C11 built with `-Wall -Wextra -Werror`, so a warning is a build failure.
`CC` and `CFLAGS` can be overridden on the command line; the warning flags are kept even then.

`make install` copies the header to `$(PREFIX)/include/surface/`, the archive to
`$(PREFIX)/lib` (`LIBDIR=` to change it) and a pkg-config file, so a consumer builds with:

```
cc app.c $(pkg-config --cflags --libs libsurface)
```

(`--static` adds libm for a link against the archive.)

## Tests

Every test runs through `tests/run-test.sh`, which gives it a timeout: a hang is a failure,
never a stalled build. A test that exits 77 proved this machine cannot host it and is reported
as SKIP, never as a pass.

The suites need no device. `tests/harness.h` is a fake backend that records every write, a
fake host that answers the surface's moves the way a console does (echoing at once, lagging,
or refusing), and a virtual clock that runs the surface's timers in order:

- `test_motor`: the motor policy, case by case as the X-Touch driver's suite proved it;
- `test_audit`: the motor-law audit of 2026-09-25 and the fast-throw round trip;
- `test_bank`: the bank window;
- `test_meter`: meter ballistics, ring rendering, and the step law against
  `tests/golden/meter-steps.txt`, vectors generated once from the TypeScript law and fixed;
- `test_c24`: the Control|24's rules: release debounce, set-point echo, do-not-drive,
  do-not-read, the jump filter, the quarantine, the replay on every session.

The script checks: `constants-check.sh` (each timing constant declared once, in the header),
`conformance-packaging.sh` (the version and soname agree in every packaging copy),
`test_install.sh` (a consumer builds against an installed tree through pkg-config alone),
`test_workflows.sh` (the workflows parse), and `public-sources-check.sh` with its own test.
