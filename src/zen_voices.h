#pragma once

// The eight voice slots, transcribed from the ZenTouch web manual (04 Voices).
//
// brightness and damping are the *centre* values for the Timbre and Linger
// controls: with both knobs at noon the voice sounds exactly as authored, and
// the knobs move away from that point in either direction. Non-linearity is
// preset-only and not exposed to the player.

namespace zen
{
static constexpr int kNumVoices = 8;

enum class Character
{
    None,
    PitchDip,    // -55 cents over 170 ms
    AttackSwell, // 35 ms
    Tremolo,     // 3 Hz, 0.35 depth, downward only
    Vibrato,     // 5.5 Hz, +/-20 cents, ramped in after the attack
    Shimmer,     // 0.03 continuous noise injection
};

struct VoicePreset
{
    const char* name;
    float       brightness; // Timbre centre
    float       damping;    // the manual's figure, kept for reference only
    float       decay_s;    // authored T60 in seconds -- Linger centre
    float       nonlin;     // preset-only
    Character   character;
};

// decay_s is a log interpolation of the manual's damping figures across
// 0.8s (most damped, Stillness at 0.40) to 3.5s (least, Hymn at 0.03). The
// figures themselves could not be used directly: measured against DaisySP's
// String they land far outside any musical range -- see the note below.
static constexpr VoicePreset kVoices[kNumVoices] = {
    {"Stillness", 0.60f, 0.40f, 0.80f,  0.00f, Character::None},
    {"Breath",    0.50f, 0.30f, 1.19f,  0.00f, Character::PitchDip},
    {"Daybreak",  0.75f, 0.22f, 1.62f,  0.00f, Character::None},
    {"Halo",      0.60f, 0.15f, 2.15f, -0.10f, Character::AttackSwell},
    {"Lullaby",   0.90f, 0.10f, 2.59f,  0.00f, Character::Tremolo},
    {"Clarity",   1.00f, 0.05f, 3.25f,  0.00f, Character::None},
    {"Hymn",      1.00f, 0.03f, 3.50f, -0.45f, Character::Vibrato},
    {"Stardust",  0.95f, 0.04f, 3.37f, -0.25f, Character::Shimmer},
};

// Decay is set by an explicit amplitude envelope, not by the string's own loss.
//
// Measured on a host build of DaisySP's String: its natural T60 depends far
// more on brightness and pitch than on the damping parameter -- 2.0s to 18s
// across the brightness range at fixed damping, and 13.7s at A1 against 2.4s at
// C5 for identical settings. Decay was therefore a side effect of timbre and
// pitch rather than something Linger controlled, and every voice's authored
// value landed in a region where notes effectively never stopped.
//
// The string is now run at a fixed damping chosen so it always outlasts the
// envelope, and the envelope alone defines T60. The one corner where the string
// still runs out first is the dullest timbre at the top of the range: 3.35s
// against a 4s target.
static constexpr float kToneDamping  = 0.70f;
static constexpr float kLongestT60   = 4.0f; // Linger fully counter-clockwise
static constexpr float kShortestT60  = 0.3f; // Linger fully clockwise

// Character feature constants.
//
// Slots 4-7 (Lullaby, Clarity, Hymn, Stardust) all sit at brightness 0.90-1.00
// and decay 0.90-0.97, so on this hardware they are told apart almost entirely
// by the feature below rather than by tone. The three modulated ones are
// therefore pushed past the manual's figures until they actually carry;
// Clarity keeps no feature at all and stays the plain reference of the four.
//
// The rates are unchanged -- it is the depths that were too polite.
static constexpr float kPitchDipCents   = -55.0f;
static constexpr float kPitchDipMs      = 170.0f;
static constexpr float kAttackSwellMs   = 35.0f;
static constexpr float kTremoloHz       = 3.0f;
static constexpr float kTremoloDepth    = 0.60f; // was 0.35: ~8dB pulse, not ~3.7dB
static constexpr float kVibratoHz       = 5.5f;
static constexpr float kVibratoCents    = 35.0f; // was 20: sings without wobbling
static constexpr float kVibratoRampMs   = 120.0f;
// 0.05 was too far: injected continuously into a loop at 0.96 decay, the
// level builds rather than settling. Sanitize() now bounds it either way.
static constexpr float kShimmerAmount   = 0.035f;

// Pitch modulation is recomputed once every N samples rather than every sample.
// powf is expensive and DaisySP's String already spends two of them plus an
// atanf per sample per voice; adding a third for vibrato was pushing the audio
// callback over budget once the pool grew to eight.
//
// At 48kHz this still updates pitch at 1.5kHz, against a 5.5Hz vibrato and a
// 170ms dip -- hundreds of steps per cycle, so nothing is audibly stepped. The
// vibrato LFO is clocked at kVibratoHz * this interval to compensate for being
// advanced once per block of N rather than once per sample.
static constexpr int kPitchUpdateInterval = 32;

} // namespace zen
