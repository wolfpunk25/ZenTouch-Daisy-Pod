#include "zen_fx.h"
#include "zen_util.h"
#include "dev/sdram.h"
#include <cmath>

using namespace daisysp;

namespace zen
{
// The echo lines live in SDRAM. They are small enough to sit in SRAM, but the
// looper will want the fast internal memory later, so they start out of the way.
static DelayLine<float, kEchoMaxSamples> DSY_SDRAM_BSS echo_line_l;
static DelayLine<float, kEchoMaxSamples> DSY_SDRAM_BSS echo_line_r;

void Fx::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    body_.Init(sample_rate);
    body_.SetFreq(kBodyFreq);
    body_.SetRes(0.85f);
    body_.SetDrive(0.0f);

    hole_.Init(sample_rate);
    hole_.SetFreq(kSoundHoleFreq);
    hole_.SetRes(0.80f);
    hole_.SetDrive(0.0f);

    // Two independent engines, each feeding one side, so the field opens
    // outwards rather than just widening a shared signal.
    chorus_l_.Init(sample_rate);
    chorus_l_.SetLfoFreq(kChorusLfoL);
    chorus_l_.SetLfoDepth(0.7f);
    chorus_l_.SetDelayMs(12.0f);
    chorus_l_.SetFeedback(0.15f);

    chorus_r_.Init(sample_rate);
    chorus_r_.SetLfoFreq(kChorusLfoR);
    chorus_r_.SetLfoDepth(0.7f);
    chorus_r_.SetDelayMs(16.0f);
    chorus_r_.SetFeedback(0.15f);

    echo_line_l.Init();
    echo_line_r.Init();
    const float echo_samples = kEchoTimeMs * 0.001f * sample_rate;
    echo_line_l.SetDelay(echo_samples);
    echo_line_r.SetDelay(echo_samples);

    echo_damp_l_.Init(sample_rate);
    echo_damp_l_.SetFreq(kEchoDampHz);
    echo_damp_l_.SetRes(0.0f);
    echo_damp_r_.Init(sample_rate);
    echo_damp_r_.SetFreq(kEchoDampHz);
    echo_damp_r_.SetRes(0.0f);

    limit_gain_    = 1.0f;
    limit_release_ = 1.0f - expf(-1.0f / (kLimitRelMs * 0.001f * sample_rate));
}

float Fx::ProcessLimiter(float& l, float& r)
{
    // Stereo-linked peak limiter: instant attack, exponential release.
    l = Sanitize(l);
    r = Sanitize(r);
    const float peak = fmaxf(fabsf(l), fabsf(r));
    const float target = (peak > kLimitThresh) ? (kLimitThresh / peak) : 1.0f;

    if(target < limit_gain_)
        limit_gain_ = target;
    else
        limit_gain_ += (target - limit_gain_) * limit_release_;

    l *= limit_gain_;
    r *= limit_gain_;
    return limit_gain_;
}

void Fx::Process(float dry_mono, float in_l, float in_r, float* out_l, float* out_r)
{
    // --- Resonance: additive body colour, still mono. ---
    dry_mono = Sanitize(dry_mono);

    body_.Process(dry_mono);
    hole_.Process(dry_mono);
    const float res_wet = (body_.Band() + hole_.Band()) * resonance_;
    const float sig     = dry_mono + res_wet;

    // --- Ambient: the mono-to-stereo stage, equal-power crossfade. ---
    const float dry_g = cosf(ambient_ * HALFPI_F);
    const float wet_g = sinf(ambient_ * HALFPI_F);
    float       l     = sig * dry_g + chorus_l_.Process(sig) * wet_g;
    float       r     = sig * dry_g + chorus_r_.Process(sig) * wet_g;

    // --- Echo: additive cross-fed ping-pong, dry stays full level. ---
    const float tap_l = echo_line_l.Read();
    const float tap_r = echo_line_r.Read();

    echo_damp_l_.Process(tap_l);
    echo_damp_r_.Process(tap_r);
    const float fb_l = echo_damp_l_.Low();
    const float fb_r = echo_damp_r_.Low();

    // Cross-feed is what makes the repeats walk across the image.
    echo_line_l.Write(Sanitize(l + fb_r * kEchoFeedback));
    echo_line_r.Write(Sanitize(r + fb_l * kEchoFeedback));

    l += tap_l * echo_;
    r += tap_r * echo_;

    // --- Line in, summed after local FX and before the limiter. ---
    l += Sanitize(in_l);
    r += Sanitize(in_r);

    // --- Limiter, then volume, so the ceiling is fixed regardless of level. ---
    ProcessLimiter(l, r);

    l *= volume_;
    r *= volume_;

    *out_l = fclamp(l, -kSafetyClamp, kSafetyClamp);
    *out_r = fclamp(r, -kSafetyClamp, kSafetyClamp);
}

} // namespace zen
