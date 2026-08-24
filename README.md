# ZenTouch for Daisy Pod

A reimplementation of [ZenTouch](https://zentouch-touch2.vercel.app) — StubeMusic's
Karplus-Strong kalimba firmware for the Synthux Touch 2 — rebuilt for the
[Daisy Pod](https://daisy.audio/products/pod).

## What this is, and what it is not

The original ZenTouch is distributed as **compiled binaries only**; the
[upstream repository](https://github.com/ymillion/zentouch-Touch2-Firmware)
contains three `.bin` files and no source, and carries no licence file.

This project is therefore **not a port**. Nothing was disassembled or copied.
It is an independent implementation written against the behaviour published in
the ZenTouch web manual — the voice presets, scale tables, effect constants,
polyphony rules and control curves are all transcribed from that public
documentation, and the source is cited inline where each table is defined.

Flashing the stock ZenTouch `.bin` to a Pod does not work: it expects the Touch
2's I2C capacitive touch sensor, eight ADC channels, and switches on D6–D9.

If you want the real thing, the author is active on the
[Daisy forum thread](https://community.daisy.audio/t/zentouch-kalimba-and-plucked-string-firmware-for-synthux-touch-2/9551)
and takes support at [buymeacoffee.com/stubemusic](https://buymeacoffee.com/stubemusic).

## Hardware mapping

The Touch 2 has eight knobs, two three-way switches and twelve touch pads. The
Pod has two knobs, two buttons and an encoder. The surface is remapped:

| ZenTouch          | Daisy Pod                                          |
| ----------------- | -------------------------------------------------- |
| 9 tine pads       | MIDI note input (TRS MIDI in), snapped to the scale |
| S30–S37 (8 knobs) | 4 encoder-selected pages × knob 1 / knob 2         |
| Switch A interval | Button 2 — cycles root → fifth → third             |
| Switch B octave   | Button 1 — cycles base → up → down                 |
| —                 | Encoder click — all notes off                       |

### Pages

Turn the encoder to change page. LED 1 shows which one you are on.

| Page | LED 1 colour | Knob 1    | Knob 2  |
| ---- | ------------ | --------- | ------- |
| 0    | red          | Linger    | Timbre  |
| 1    | green        | Voice     | Scale   |
| 2    | blue         | Ambient   | Echo    |
| 3    | amber        | Resonance | Volume  |

Because two knobs serve eight parameters, each knob is **soft-takeover**: after
a page change it does nothing until it passes through the stored value, so
nothing jumps when you land on a new page.

LED 2 shows octave shift as a colour (green = base, red = up, blue = down),
tints violet when interval doubling is on, and flashes white on each note.

### Not available on the Pod

The Pod's volume knob is an **analog attenuator wired to the headphone amp**.
Firmware cannot read it, so ZenTouch's S37 Volume lives on page 3 instead and
the physical knob trims your headphones on top of it.

The Pod has MIDI **in** only — there is no MIDI out jack.

## Build

Requires an `arm-none-eabi` toolchain on `PATH`.

```bash
git submodule update --init --recursive
git apply --directory=DaisySP patches/daisysp-karplus-string.patch
make -C libDaisy && make -C DaisySP && make
```

### The DaisySP patch is required

`patches/daisysp-karplus-string.patch` carries two fixes to `KarplusString`.

**Unreachable curved-bridge mode.** `SetNonLinearity` clamps its argument to
`0..1`, while `Process` selects the curved-bridge mode on **negative** values
and the header documents the range as `-1 to 1`. The branch is unreachable
upstream. Three ZenTouch voices — Halo (−0.10), Stardust (−0.25) and Hymn
(−0.45) — depend on it, so the clamp is widened to `-1..1`.

**Per-sample recomputation of constants.** `ProcessInternal` derived
`damping_cutoff` from `damping_` and `brightness_` on every sample, then spent
two `powf` and an `atanf` on it — despite both inputs only changing when a
setter is called. With eight voices this measured at roughly half the audio
budget and pushed the callback past 100%. The derived values are now cached and
recomputed only when a setter marks them dirty; only `damping_f`, which
genuinely varies with `frequency_`, is still computed per sample.

## Flash

Put the Daisy into bootloader mode: hold **BOOT**, tap **RESET**, release
**RESET**, then release **BOOT**. It enumerates as `STM32 BOOTLOADER`
(`0483:df11`). Then either:

```bash
make program-dfu
```

or drag `build/ZenTouchPod.bin` into the
[Daisy Web Programmer](https://electro-smith.github.io/Programmer/).

## Status

Implemented: 8 voice presets with their character features, the 12-scale Zen
Arc, 5-voice polyphony with 30 ms steal fades, ±8% trigger humanisation,
scale-aware interval doubling, octave shift, the resonance / ambient / echo /
limiter FX bus, stereo line-in summing, and MIDI note input.

Not implemented: the audio-buffer looper and the QSPI calibration flow. The
calibration exists to correct Touch 2 pot tolerance and is largely moot with two
knobs. The looper needs roughly 23 MB of SDRAM (there is plenty) but the build
is already at 87% of the 128 KB internal flash, so it will want the Daisy
bootloader and a QSPI build.
