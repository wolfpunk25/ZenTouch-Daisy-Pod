#pragma once

namespace zen
{
// Bound a sample: NaN and infinities become silence, runaway values clamp.
//
// This is not belt-and-braces. The long-decay voices run the Karplus-Strong
// loop very close to unity gain, and Stardust injects noise into that loop
// continuously, so its level can build instead of decaying. A single infinity
// reaching the limiter turns the entire FX bus to NaN -- the limiter computes
// threshold/Inf = 0, then Inf * 0 = NaN -- and because the echo feeds its
// output back at 0.5 gain, the NaN circulates forever. Audio dies permanently
// while the LEDs carry on, and only a power cycle clears it.
//
// The comparison is written inverted so NaN, for which every comparison is
// false, falls through to the guarded branch.
static inline float Sanitize(float x)
{
    if(x > -8.0f && x < 8.0f)
        return x;
    if(x > 0.0f)
        return 8.0f;
    if(x < 0.0f)
        return -8.0f;
    return 0.0f; // NaN
}

} // namespace zen
