#pragma once
#include <cstdint>

// The 12-scale "Zen Arc", transcribed from the ZenTouch web manual (05 Scales).
// Each scale is nine semitone offsets from the root. Tine 0 is the root; the
// remaining degrees fan outwards from the centre in kalimba order.
//
// Ordering is deliberate: adjacent slots share most of their pitches, so
// sweeping the scale control feels like a dimmer rather than a switch. The two
// intentional mode shifts are 3->4 and 7->8.

namespace zen
{
static constexpr int kNumScales = 12;
static constexpr int kNumTines  = 9;

struct Scale
{
    const char* name;
    int8_t      offset[kNumTines]; // semitones from root
};

static constexpr Scale kScales[kNumScales] = {
    {"Yo",        {0, 2, 5, 7,  9, 12, 14, 17, 19}}, //  0 Japanese folk
    {"Bhupali",   {0, 2, 4, 7,  9, 12, 14, 16, 19}}, //  1 Indian raga, evening
    {"Akebono",   {0, 2, 3, 7,  9, 12, 14, 15, 19}}, //  2 Japanese dawn
    {"Hirajoshi", {0, 2, 3, 7,  8, 12, 14, 15, 19}}, //  3 Japanese koto
    {"Yu",        {0, 3, 5, 7, 10, 12, 15, 17, 19}}, //  4 Chinese / Min'yo
    {"Shang",     {0, 2, 5, 7, 10, 12, 14, 17, 19}}, //  5 Chinese pentatonic
    {"Insen",     {0, 1, 5, 7, 10, 12, 13, 17, 19}}, //  6 Japanese yin
    {"Iwato",     {0, 1, 5, 6, 10, 12, 13, 17, 18}}, //  7 Japanese dark
    {"Sakura",    {0, 1, 5, 7,  8, 12, 13, 17, 19}}, //  8 Japanese In
    {"Hijaz",     {0, 1, 4, 5,  7,  8, 10, 12, 13}}, //  9 Arabic maqam
    {"Bhairav",   {0, 1, 4, 5,  7,  8, 11, 12, 13}}, // 10 Indian raga, dawn
    {"Marwa",     {0, 1, 4, 6,  7,  9, 11, 12, 13}}, // 11 Indian raga, twilight
};

// Pitch-class membership, folded from the offsets so the mask can never drift
// out of sync with the table above.
constexpr uint16_t PitchClassMask(const Scale& s)
{
    uint16_t mask = 0;
    for(int i = 0; i < kNumTines; i++)
        mask |= static_cast<uint16_t>(1u << (s.offset[i] % 12));
    return mask;
}

constexpr bool ScaleContains(const Scale& s, int semitone)
{
    return (PitchClassMask(s) >> (((semitone % 12) + 12) % 12)) & 1u;
}

// Switch A "down" doubles each tine with a third: major when the pitch four
// semitones up is in the scale, otherwise minor, so the added voice always
// stays inside the mode.
constexpr int ThirdInterval(const Scale& s, int tine_semitone)
{
    return ScaleContains(s, tine_semitone + 4) ? 4 : 3;
}

static constexpr int kFifthInterval = 7;

} // namespace zen
