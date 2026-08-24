# Upstream reports for DaisySP `KarplusString`

Two issues found while building this firmware, both carried in
`daisysp-karplus-string.patch`. Neither has been submitted yet — this is a
prepared writeup, ready to file at
[electro-smith/DaisySP](https://github.com/electro-smith/DaisySP/issues).

Found against `DaisySP` at `599511b` (V1.0.0-25-g599511b), built for
`STM32H750xx` with `-O3`.

---

## 1. `String::SetNonLinearity` makes curved-bridge mode unreachable

`Process` dispatches on the sign of `non_linearity_amount_`:

```cpp
float String::Process(const float in)
{
    if(non_linearity_amount_ <= 0.0f)
    {
        non_linearity_amount_ *= -1;
        float ret = ProcessInternal<NON_LINEARITY_CURVED_BRIDGE>(in);
        non_linearity_amount_ *= -1;
        return ret;
    }
    else
    {
        return ProcessInternal<NON_LINEARITY_DISPERSION>(in);
    }
}
```

The header documents the range accordingly:

```cpp
/** Set the string's behavior.
    \param -1 to 0 is curved bridge, 0 to 1 is dispersion.
*/
void SetNonLinearity(float non_linearity_amount);
```

But the setter clamps negatives away:

```cpp
void String::SetNonLinearity(float non_linearity_amount)
{
    non_linearity_amount_ = fclamp(non_linearity_amount, 0.f, 1.f);
}
```

So `non_linearity_amount_` can only ever be `>= 0`, and
`NON_LINEARITY_CURVED_BRIDGE` is dead code — the only way in is `== 0.0f`,
where the amount is zero and the mode has no effect.

**Fix:** widen the clamp to match the documented range.

```diff
-    non_linearity_amount_ = fclamp(non_linearity_amount, 0.f, 1.f);
+    non_linearity_amount_ = fclamp(non_linearity_amount, -1.f, 1.f);
```

`StringVoice` is unaffected: its `SetStructure` maps to non-negative values
only, so it never reached curved-bridge either way.

---

## 2. `ProcessInternal` recomputes per-note constants every sample

`damping_cutoff` derives entirely from `damping_` and `brightness_`, both of
which only change when a setter runs. Two `powf` and one `atanf` are derived
from it **on every sample, for every voice**:

```cpp
float damping_cutoff
    = fmin(12.0f + damping_ * damping_ * 60.0f + brightness * 24.0f, 84.0f);
float damping_f
    = fmin(frequency_ * powf(2.f, damping_cutoff * kOneTwelfth), 0.499f);
// ...
float ratio                = powf(2.f, damping_cutoff * kOneTwelfth);
float damping_compensation = 1.f - 2.f * atanf(1.f / ratio) / (TWOPI_F);
```

Only `damping_f` genuinely varies per sample, and only through `frequency_` —
which reduces to a single multiply once the `powf` is hoisted.

**Measured impact.** Eight `String` voices plus a chorus, ping-pong delay and
limiter, at 48kHz with a 48-sample block on an STM32H750 at 480MHz, using
`CpuLoadMeter`:

| | avg | max |
| --- | --- | --- |
| Before | 77–86% | **101%** |
| After | 54% | **64%** |

At 101% the audio callback overran and the firmware locked up.

**Fix:** cache the derived values behind a dirty flag set by `SetBrightness`,
`SetDamping` and `Init`, and recompute them in `ProcessInternal` only when it is
set. Care is needed because the `damping_ >= 0.95f` "crossfade to infinite
decay" branch also modifies `brightness`, which is read later at
`noise_filter`, and modifies `damping_cutoff` before `ratio` is taken from it —
so both the adjusted brightness and the additive infinite-decay term must be
cached too. See `daisysp-karplus-string.patch` for the full change.

The behaviour is intended to be bit-identical; this is purely hoisting
loop-invariant work.
