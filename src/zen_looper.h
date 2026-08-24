#pragma once
#include <cstdint>
#include <cstddef>

namespace zen
{
// Audio-buffer looper, following the ZenTouch spec (07 Looper).
//
// The recording tap sits after the voice mixer and before the resonant body,
// so the loop captures the dry voice mix: voice, scale, timbre, linger, octave
// and interval all travel with the audio, while Ambient, Echo, Resonance and
// Volume re-apply live on every revolution because they run downstream.
//
// The headline property is that loop playback costs zero voices. It is samples
// added to the dry bus, not synth events, so the five-voice cap applies only to
// what you play live no matter how dense the loop is.

static constexpr int    kMaxLayers      = 4;                // base + 3 overdubs
static constexpr int    kMaxOverdubs    = kMaxLayers - 1;
static constexpr size_t kLoopMaxSamples = 60 * 48000;       // 60 s at 48 kHz

enum class LoopState
{
    Idle,        // also "idle with data" when Length() > 0 -- see the spec
    Armed,       // waiting for the first note to start the clock
    Recording,
    Playing,
    Overdubbing,
};

class Looper
{
  public:
    void Init();

    // Called per sample with the live dry voice mix. Returns the loop's
    // contribution, to be added to the dry bus before the FX chain. The live
    // signal is what gets recorded; the returned playback is deliberately not,
    // so overdubs never re-record the loop.
    float Process(float live_dry);

    // --- gestures, mapped from the Touch 2's P01 / P10 / P11 ---
    void Tap();         // P01 alone: advance the state machine
    void ToggleMute();  // P01 held: mute/unmute the last overdub
    void StopResume();  // P10 + P01
    void RemoveLast();  // P11 + P01
    void Clear();       // P10 + P11 held

    // The clock does not start until the first note after arming.
    void NotePlayed();

    LoopState State() const { return state_; }
    size_t    Length() const { return length_; }
    bool      HasData() const { return length_ > 0; }
    int       Overdubs() const { return overdubs_; }
    bool      LastMuted() const { return mute_last_; }

  private:
    void CloseRecording();
    void CommitOverdub();
    // Zero [from, to) within a layer, wrapping at the loop length.
    void ZeroSpan(int layer, size_t from, size_t to);

    LoopState state_     = LoopState::Idle;
    size_t    pos_       = 0;
    size_t    length_    = 0;
    int       overdubs_  = 0;   // committed overdub layers, 0..3
    bool      mute_last_ = false;

    // Overdub bookkeeping. The take's first pass over each position writes,
    // and later passes sum -- so a layer never needs megabytes zeroed inside
    // the audio callback. The pass has to be tracked from where the overdub
    // actually began, not from the loop's start: an overdub begins wherever
    // playback happened to be, so counting loop wraps would put every position
    // between the loop start and that point into sum mode having never written
    // it, summing this take onto whatever the layer held before.
    size_t overdub_start_pos_  = 0;
    bool   overdub_first_pass_ = true;
    bool   overdub_had_audio_  = false;
};

} // namespace zen
