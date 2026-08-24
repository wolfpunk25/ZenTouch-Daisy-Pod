#pragma once

#include "daisysp.h"
#include "zen_voices.h"

namespace zen
{
// One plucked string voice: a DaisySP Karplus-Strong core, a per-note
// excitation burst, and the optional character feature belonging to the slot
// that triggered it.
//
// A voice captures its preset and knob values at NoteOn and holds them for the
// life of the note. That is what lets you turn the voice or scale control while
// something is still ringing without the sound changing underneath you.
class Voice
{
  public:
    void Init(float sample_rate);

    // amp is the post-humanisation trigger amplitude (see kTrigAmp*).
    void NoteOn(float freq, float amp, int slot, float timbre, float linger);

    // Begin the ~30 ms steal fade. The voice keeps sounding until it lands.
    void FadeOut();

    float Process();

    bool     IsActive() const { return active_; }
    bool     IsFading() const { return fading_; }
    uint32_t Age() const { return age_; }

  private:
    float ExcitationSample();

    daisysp::String     string_;
    daisysp::Svf        exc_filter_;
    daisysp::Oscillator lfo_;
    daisysp::WhiteNoise noise_;

    float sample_rate_    = 48000.0f;
    float one_over_sr_    = 1.0f / 48000.0f;

    bool     active_ = false;
    bool     fading_ = false;
    uint32_t age_    = 0;

    // Excitation burst state.
    int   remaining_noise_ = 0;
    float amp_             = 0.0f;

    // Pitch, in Hz, before any character-feature modulation.
    float base_freq_ = 440.0f;

    // Character feature state, all driven from the slot captured at NoteOn.
    Character character_    = Character::None;
    float     feature_phase_ = 0.0f; // seconds since NoteOn, for ramps
    float     swell_gain_    = 1.0f;
    float     shimmer_amt_   = 0.0f;

    // Steal fade.
    float fade_gain_  = 1.0f;
    float fade_delta_ = 0.0f;

    // Amplitude envelope that actually sets the decay time.
    float env_       = 1.0f;
    float env_coeff_ = 1.0f;

    // Envelope follower, used for two things: releasing the voice once it has
    // decayed to silence, and scaling the shimmer so it rides the decay rather
    // than sustaining the string forever.
    float    level_        = 0.0f;
    uint32_t silent_count_ = 0;
};

// Split-curve knob mapping: physical noon is the authored preset value, and the
// knob travels away from it in both directions. This is what the manual calls
// "knob center equals the active voice preset value".
float MapTimbre(float knob, float preset_brightness);

// Returns the target T60 in seconds. Knob centre is the voice's authored decay
// and the ends reach the manual's 4s and 0.3s.
float MapLingerT60(float knob, float voice_decay_s);

// Trigger amplitude window from the manual's +/-8 percent humanisation.
static constexpr float kTrigAmpMin = 0.62f;
static constexpr float kTrigAmpMax = 0.78f;

} // namespace zen
