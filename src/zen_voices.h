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
    float       damping;    // Linger centre
    float       nonlin;     // preset-only
    Character   character;
};

static constexpr VoicePreset kVoices[kNumVoices] = {
    {"Stillness", 0.60f, 0.40f,  0.00f, Character::None},
    {"Breath",    0.50f, 0.30f,  0.00f, Character::PitchDip},
    {"Daybreak",  0.75f, 0.22f,  0.00f, Character::None},
    {"Halo",      0.60f, 0.15f, -0.10f, Character::AttackSwell},
    {"Lullaby",   0.90f, 0.10f,  0.00f, Character::Tremolo},
    {"Clarity",   1.00f, 0.05f,  0.00f, Character::None},
    {"Hymn",      1.00f, 0.03f, -0.45f, Character::Vibrato},
    {"Stardust",  0.95f, 0.04f, -0.25f, Character::Shimmer},
};

// Character feature constants.
static constexpr float kPitchDipCents   = -55.0f;
static constexpr float kPitchDipMs      = 170.0f;
static constexpr float kAttackSwellMs   = 35.0f;
static constexpr float kTremoloHz       = 3.0f;
static constexpr float kTremoloDepth    = 0.35f;
static constexpr float kVibratoHz       = 5.5f;
static constexpr float kVibratoCents    = 20.0f;
static constexpr float kVibratoRampMs   = 120.0f;
static constexpr float kShimmerAmount   = 0.03f;

} // namespace zen
