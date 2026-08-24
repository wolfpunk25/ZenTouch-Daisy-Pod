#pragma once
#include "daisy_pod.h"
#include "zen_scales.h"
#include "zen_voices.h"
#include "zen_looper.h"

namespace zen
{
// ZenTouch has eight always-live controls. The Pod has two knobs, so the eight
// parameters are grouped into four pages of two and the encoder selects the
// page. LED 1 shows which page you are on.
//
// Changing page leaves the physical knobs pointing at the wrong values, so each
// knob is "uncaught" after a page change and does nothing until it passes
// through the stored value. That prevents a parameter jumping the moment you
// touch a knob on a page you have just arrived at.

enum class Page
{
    LingerTimbre,   // S30, S31
    VoiceScale,     // S32, S33
    AmbientEcho,    // S34, S35
    ResonanceVolume,// S36, S37
    Count,
};

enum class IntervalMode
{
    Root,  // centre: root only
    Fifth, // +7 semitones
    Third, // scale-aware major or minor
};

enum class OctaveShift
{
    Down = -1,
    Base = 0,
    Up   = 1,
};

struct Params
{
    // All normalised 0..1, matching the original knob positions.
    float linger    = 0.5f; // S30
    float timbre    = 0.5f; // S31
    float voice     = 0.5f; // S32
    float scale     = 0.5f; // S33
    float ambient   = 0.0f; // S34
    float echo      = 0.0f; // S35
    float resonance = 0.0f; // S36
    float volume    = 0.8f; // S37

    // Discrete selections, held with hysteresis so boundary knob positions do
    // not flicker between slots.
    int voice_slot = 3; // Halo, the manual's reference start point
    int scale_slot = 5; // Shang, likewise

    IntervalMode interval = IntervalMode::Root;
    OctaveShift  octave   = OctaveShift::Base;
};

// One block's worth of looper gestures. The Touch 2 drives its looper from a
// dedicated pad plus two silent modifiers; the Pod has none spare, so the
// encoder stands in for P01 and the two buttons for P10 and P11. Buttons act as
// modifiers when the encoder is clicked while one is held, and fall back to
// their octave/interval duty on release when it was not.
struct Gestures
{
    bool tap         = false; // P01 alone
    bool hold_mute   = false; // P01 held 1.5s
    bool stop_resume = false; // P10 + P01
    bool remove_last = false; // P11 + P01
    bool clear       = false; // P10 + P11 held 2s
    bool panic       = false; // both buttons, released quickly
};

class Ui
{
  public:
    void Init(daisy::DaisyPod* pod);

    // Call at control rate, once per audio block.
    void Update();

    Params&       params() { return params_; }
    const Params& params() const { return params_; }

    Page page() const { return page_; }

    // Diagnostics: every slot the control loop has selected since boot,
    // accumulated at control rate so nothing is lost to slow logging.
    uint32_t VisitedVoices() const { return visited_voices_; }
    uint32_t VisitedScales() const { return visited_scales_; }

    // Momentary white flash on LED 2 whenever a note fires.
    void NoteFlash() { note_flash_ = 1.0f; }

    const Gestures& gestures() const { return gestures_; }

    // The looper's state drives LED 1, and decides whether an encoder press
    // fires immediately or waits to be disambiguated from a hold.
    void SetLooperState(LoopState s, int overdubs)
    {
        loop_state_    = s;
        loop_overdubs_ = overdubs;
    }

    // True for one Update() when the encoder was clicked (all-notes-off).
    bool PanicRequested() const { return panic_; }

  private:
    void  UpdateKnob(float raw, float* target, int index);
    void  UpdateLeds();
    void  ResetCatch();

    daisy::DaisyPod* pod_ = nullptr;
    Params           params_;
    Page             page_ = Page::LingerTimbre;

    bool  caught_[2]   = {false, false};
    float last_raw_[2] = {0.0f, 0.0f};

    uint32_t visited_voices_ = 0;
    uint32_t visited_scales_ = 0;

    float note_flash_ = 0.0f;
    float page_flash_ = 0.0f;
    bool  panic_      = false;

    Gestures  gestures_;
    LoopState loop_state_    = LoopState::Idle;
    int       loop_overdubs_ = 0;

    // Modifier bookkeeping: a button that acted as a modifier must not also
    // fire its own action when released.
    bool b1_consumed_ = false, b2_consumed_ = false;
    bool combo_active_ = false, combo_fired_ = false;
    bool enc_deferred_ = false, enc_hold_fired_ = false;

    float    clear_progress_ = 0.0f; // 0..1 through the 2s clear hold
    uint32_t tick_           = 0;    // Update() calls, ~1 per ms
};

// Discrete slot selection with hysteresis, shared by the voice and scale knobs.
int SlotWithHysteresis(float knob, int num_slots, int current);

} // namespace zen
