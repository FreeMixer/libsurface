# libsurface

The device behaviour of a motorised control surface, written once in C for every surface and every host: the motor-fader policy, touch, the bank window, a shadow of the whole board and its replay, meter steps and LED ring rendering. A Behringer X-Touch, a Digidesign Control|24 and, later, a ProControl run on the same code; a mixer or DAW links it and keeps only its mixing semantics (what a strip shows, names, colours, its verbs).

The design is [FreeMixer/libdiginet#29](https://github.com/FreeMixer/libdiginet/issues/29). This repository holds its phase P1, the core. The backends (`libsurface-mcu` over libmackie, `libsurface-diginet` over [libdiginet](https://github.com/FreeMixer/libdiginet)) and the Python binding come in later phases; the backend interface they plug into is already in the header.

## What it does

- **Motor faders that never fight the hand.** A fader has a touch flag and a target; the motor tracks the target only while untouched, and on release the cap is written once, to where the hand left it. An implicit-touch window holds the motor off while reports say a hand moves the cap even when the touch sensor drops out. The surface's own moves are awaited until the host echoes them, so a lagging burst of echoes never walks the cap back through the gesture, and a burst of served frames costs one leading and one trailing write.
- **Touch** with a self-heal for a release lost on the wire (a surface that repeats its touch message while held), and a release debounce for a sensor that chatters (the Control|24's drops out for 30-240 ms during a move).
- **The Control|24's set-point.** Its motor holds a set-point of its own and drives back to it when the cap is let go, and the desk never reports its own moves; so while a hand is on the cap each reported position is written back as the set-point, and once more at the release.
- **Faders that cannot be trusted:** a do-not-drive list, a do-not-read list, an impossible-jump filter, and a quarantine of a fader whose sensor keeps jumping.
- **Bank window** over the host's ordered list of targets: bank and channel steps, show, jump.
- **Board shadow and replay.** The latest value of every element (LEDs, rings, scribbles, counter, colours, fader targets) is kept; a device that comes back blank gets the whole board again without asking the host.
- **Meter steps** with hysteresis, sent only on a change, with a keep-alive for a surface that decays a lit meter by itself.

What it is not: it speaks no wire, owns no socket, thread or clock, and knows no channel or verb. Each call that needs time takes the caller's `now_us`; `surface_deadline()` and `surface_tick()` run the timers. It allocates once, in `surface_create()`.

## The API

Everything is in [`include/surface/surface.h`](include/surface/surface.h).

```c
#include <surface/surface.h>

struct surface_config c = { .ops = &my_backend_ops, .backend = dev, .event = on_event, .event_ctx = host };
surface_geometry_preset(SURFACE_MODEL_CONTROL24, &c.geometry);
struct surface *s;
surface_create(&s, &c);

surface_bank_set(s, targets, n);                 /* what the strips show: opaque ids of the host's */
surface_in_online(s, now_us);                    /* the backend: the device is ready; the board replays */

surface_fader_served(s, target, 0.75, now_us);   /* the host's truth -> the motor, by the policy */
surface_scribble(s, 0, 0, "KICK");
surface_meter(s, 0, 0, fill, now_us);

surface_in_fader(s, strip, raw, now_us);         /* the backend's decoded input ... */
/* ... comes up as SURFACE_EV_FADER_MOVED / _TOUCHED / _RELEASED, _KEY, _VPOT, _JOG, _WARNING */

uint64_t when = surface_deadline(s, now_us);     /* sleep until then, or until input */
surface_tick(s, when);
```

Up the seam go events with the strip's target; down go fader targets, LED states by key id, ring values, scribble text, meter fills, counter text and colours. MIDI bytes, DigiNet frames, channel ids and verb names never cross it.

## Building

`make`, `make test`, `make install`. A C11 compiler, `make` and libm; nothing else. See [BUILDING.md](BUILDING.md).

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
