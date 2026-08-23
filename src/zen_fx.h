#pragma once
#include "daisysp.h"

namespace zen
{
// The master FX bus, in the order the manual's signal chain describes:
//
//   voices (mono) -> Resonance -> Ambient -> Echo -> [line in] -> Limiter -> Volume
//
// Resonance and Echo are additive: the dry signal stays at full level right
// across their sweeps and the wet content is added on top. Ambient is the one
// genuinely wet/dry stage, and it is also where the signal becomes stereo.

// Fixed constants, all taken from the manual (02 Signal / 03 Controls).
static constexpr float kBodyFreq      = 250.0f;  // wooden box
static constexpr float kSoundHoleFreq = 880.0f;  // sound hole
static constexpr float kChorusLfoL    = 0.32f;
static constexpr float kChorusLfoR    = 0.47f;
static constexpr float kEchoTimeMs    = 380.0f;
static constexpr float kEchoFeedback  = 0.50f;
static constexpr float kEchoDampHz    = 4000.0f;
static constexpr float kLimitThresh   = 0.85f;
static constexpr float kLimitRelMs    = 80.0f;
static constexpr float kSafetyClamp   = 0.95f;

static constexpr size_t kEchoMaxSamples = 20000; // 380 ms @ 48 kHz = 18240

class Fx
{
  public:
    void Init(float sample_rate);

    void SetResonance(float amount) { resonance_ = amount; }
    void SetAmbient(float amount) { ambient_ = amount; }
    void SetEcho(float amount) { echo_ = amount; }
    void SetVolume(float amount) { volume_ = amount; }

    // dry_mono is the summed voice output; in_l/in_r are the stereo line in,
    // which is summed after the local FX and before the limiter so that a
    // chained upstream unit arrives untouched by this unit's effects.
    void Process(float dry_mono, float in_l, float in_r, float* out_l, float* out_r);

  private:
    float ProcessLimiter(float& l, float& r);

    daisysp::Svf         body_, hole_;
    daisysp::ChorusEngine chorus_l_, chorus_r_;
    daisysp::Svf         echo_damp_l_, echo_damp_r_;

    float sample_rate_ = 48000.0f;

    float resonance_ = 0.0f;
    float ambient_   = 0.0f;
    float echo_      = 0.0f;
    float volume_    = 0.8f;

    // Limiter state, stereo-linked so the image never shifts under gain
    // reduction.
    float limit_gain_    = 1.0f;
    float limit_release_ = 0.0f;
};

} // namespace zen
