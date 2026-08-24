#include "zen_voice.h"
#include "zen_util.h"
#include <cmath>
#include <cstdlib>

using namespace daisysp;

namespace zen
{
// DaisySP's String takes a *decay* parameter: higher values ring longer, and
// anything at or above 0.95 heads towards infinite sustain. The manual's
// per-voice "damping" figure runs the other way (higher = more damped), which
// is why Clarity at 0.05 is the drone-like one. Invert on the way in.
static constexpr float kLongestDecay  = 0.97f; // knob fully counter-clockwise
static constexpr float kShortestDecay = 0.45f; // knob fully clockwise
static constexpr float kMinBrightness = 0.05f; // manual: clamped to 0.05-1.00

float MapTimbre(float knob, float preset_brightness)
{
    knob = fclamp(knob, 0.0f, 1.0f);
    if(knob < 0.5f)
        return kMinBrightness
               + (preset_brightness - kMinBrightness) * (knob * 2.0f);
    return preset_brightness
           + (1.0f - preset_brightness) * ((knob - 0.5f) * 2.0f);
}

float MapLinger(float knob, float preset_damping)
{
    knob            = fclamp(knob, 0.0f, 1.0f);
    const float mid = 1.0f - preset_damping;
    if(knob < 0.5f)
        return mid + (kLongestDecay - mid) * (1.0f - knob * 2.0f);
    return mid - (mid - kShortestDecay) * ((knob - 0.5f) * 2.0f);
}

void Voice::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    one_over_sr_ = 1.0f / sample_rate;

    string_.Init(sample_rate);
    exc_filter_.Init(sample_rate);
    noise_.Init();

    lfo_.Init(sample_rate);
    lfo_.SetWaveform(Oscillator::WAVE_SIN);
    lfo_.SetAmp(1.0f);

    active_ = false;
    fading_ = false;
}

void Voice::NoteOn(float freq, float amp, int slot, float timbre, float linger)
{
    const VoicePreset& p = kVoices[slot];

    base_freq_ = freq;
    amp_       = amp;
    character_ = p.character;

    // Reset the string so a long-decaying previous note on this slot cannot
    // bleed its buffer into the new one (the manual's cross-slot bleed guard).
    string_.Reset();
    string_.SetFreq(freq);
    string_.SetNonLinearity(p.nonlin);
    string_.SetBrightness(MapTimbre(timbre, p.brightness));
    string_.SetDamping(MapLinger(linger, p.damping));

    // One period of noise, low-passed in sympathy with the brightness, is the
    // standard Karplus-Strong pluck excitation.
    const float f0   = fclamp(freq * one_over_sr_, 0.0001f, 0.25f);
    remaining_noise_ = static_cast<int>(1.0f / f0);
    const float bright_norm = MapTimbre(timbre, p.brightness);
    const float cutoff      = fminf(4.0f * f0
                                  * powf(2.0f,
                                         kOneTwelfth
                                             * (bright_norm * (2.0f - bright_norm)
                                                - 0.5f)
                                             * 72.0f),
                              0.499f);
    exc_filter_.SetFreq(cutoff * sample_rate_);
    exc_filter_.SetRes(0.5f);

    feature_phase_ = 0.0f;
    swell_gain_    = (character_ == Character::AttackSwell) ? 0.0f : 1.0f;
    shimmer_amt_   = (character_ == Character::Shimmer) ? kShimmerAmount : 0.0f;

    if(character_ == Character::Tremolo)
        lfo_.SetFreq(kTremoloHz); // advanced every sample
    else if(character_ == Character::Vibrato)
        lfo_.SetFreq(kVibratoHz * kPitchUpdateInterval); // advanced every Nth
    lfo_.Reset();

    fade_gain_  = 1.0f;
    fade_delta_ = 0.0f;
    active_     = true;
    fading_     = false;
    age_        = 0;
}

void Voice::FadeOut()
{
    if(!active_ || fading_)
        return;
    fading_     = true;
    fade_delta_ = fade_gain_ / (0.030f * sample_rate_); // ~30 ms
}

float Voice::ExcitationSample()
{
    float temp = 0.0f;
    if(remaining_noise_ > 0)
    {
        temp = noise_.Process() * amp_;
        remaining_noise_--;
    }
    exc_filter_.Process(temp);
    return exc_filter_.Low();
}

float Voice::Process()
{
    if(!active_)
        return 0.0f;

    age_++;
    feature_phase_ += one_over_sr_;

    // Features that bend pitch have to be applied before the string runs. They
    // are throttled to every kPitchUpdateInterval samples -- see the note on
    // that constant for why.
    const bool update_pitch = (age_ % kPitchUpdateInterval) == 0;
    switch(update_pitch ? character_ : Character::None)
    {
        case Character::PitchDip:
        {
            // Starts 55 cents flat and bends up to true pitch over 170 ms.
            const float t = fminf(feature_phase_ / (kPitchDipMs * 0.001f), 1.0f);
            const float cents = kPitchDipCents * (1.0f - t);
            string_.SetFreq(base_freq_ * powf(2.0f, cents / 1200.0f));
            break;
        }
        case Character::Vibrato:
        {
            // Ramped in after a clean attack so the transient stays honest.
            const float ramp
                = fminf(feature_phase_ / (kVibratoRampMs * 0.001f), 1.0f);
            const float cents = kVibratoCents * ramp * lfo_.Process();
            string_.SetFreq(base_freq_ * powf(2.0f, cents / 1200.0f));
            break;
        }
        default: break;
    }

    float exc = ExcitationSample();
    if(shimmer_amt_ > 0.0f)
        exc += noise_.Process() * shimmer_amt_;

    float out = string_.Process(exc);

    // Amplitude-domain features.
    if(character_ == Character::AttackSwell)
    {
        const float t = fminf(feature_phase_ / (kAttackSwellMs * 0.001f), 1.0f);
        swell_gain_   = t;
        out *= swell_gain_;
    }
    else if(character_ == Character::Tremolo)
    {
        // Downward-only: the pulse dips below unity, it never boosts.
        const float depth = kTremoloDepth * 0.5f * (1.0f - lfo_.Process());
        out *= (1.0f - depth);
    }

    if(fading_)
    {
        fade_gain_ -= fade_delta_;
        if(fade_gain_ <= 0.0f)
        {
            fade_gain_ = 0.0f;
            active_    = false;
            fading_    = false;
            return 0.0f;
        }
        out *= fade_gain_;
    }

    return Sanitize(out);
}

} // namespace zen
