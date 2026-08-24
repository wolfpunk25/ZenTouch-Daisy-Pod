# Looper: design notes, not yet built

The audio-buffer looper is the main unimplemented ZenTouch feature. This is the
groundwork for it — deliberately stopping short of code, for reasons in the last
section.

## Budget

Memory is not the constraint. ZenTouch uses ~23MB of SDRAM for a base layer plus
three overdubs at 60s each, mono int16 @ 48kHz. The Pod's Seed has 64MB and this
firmware currently uses 160KB of it, so there is room several times over.

CPU is the constraint, and it is the open question. Current measured load is
**54% avg / 64% peak** with eight voices busy. The looper adds, per sample: one
SDRAM read for playback, one SDRAM write while recording, and mixing into the FX
bus. SDRAM access is materially slower than SRAM, and the echo already has two
delay lines there. That could be comfortably absorbed or could push the callback
back over 100% — and 101% is exactly what was locking the firmware up before.

**This must be measured, not estimated.** Build it behind `ZEN_DEBUG=1`, watch
`CpuLoadMeter` with eight voices and a three-overdub loop running, and only then
decide. If it is tight, moving the echo lines from SDRAM to SRAM buys headroom —
they are only 160KB and SRAM has 380KB free.

## The control problem

On the Touch 2 the looper has a dedicated pad (P01) plus two silent modifiers
(P10, P11). The Pod has two buttons and an encoder, all already assigned:

| Control | Currently |
| --- | --- |
| Encoder turn | page select |
| Encoder click | all notes off |
| Button 1 | octave cycle |
| Button 2 | interval cycle |

Nothing is free, so something has to give. Three options, none obviously right:

**A — Encoder gestures.** Short click cycles arm → close → overdub; hold 2s
clears. This mirrors ZenTouch's own P01 gesture and its held-2-second hard clear
most closely. Cost: all-notes-off has to move, probably to both buttons at once.

**B — A fifth page.** Encoder adds a "Looper" page where the buttons become
transport and the knobs become loop level and blend. Cost: octave and interval
become unreachable while you are on that page, which breaks the "no hidden edit
state" principle the original is explicit about.

**C — Long-press the existing buttons.** Short press keeps octave/interval, long
press drives the looper. Cost: long-press is invisible to a new player and there
is no display to teach it.

A is my inclination — it matches the original's gestures and keeps the one-layer
interface — but it changes a control that already has muscle memory attached.

## Why this stopped at notes

Two reasons, both about not being able to check the work:

1. **The CPU cost is unknown and this is exactly how the firmware broke before.**
   Writing several hundred lines of audio code that cannot be measured on
   hardware risks handing over something that locks up under load.
2. **The control scheme is a genuine judgement call**, and this session's record
   on unverified guesses is poor — the NaN theory, the delay-line theory and the
   Clarity theory were all plausible and all wrong.

The state machine, buffer layout and mixing are independent of which control
triggers them, so that work is not wasted whichever option is chosen.
