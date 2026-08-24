#include "zen_ui.h"
#include "daisysp.h"
#include <cmath>

using namespace daisy;

namespace zen
{
// Margin, in slot widths, that the knob must travel past a boundary before the
// slot changes. Keeps noon from rattling between two voices.
static constexpr float kHysteresis = 0.15f;

// Smallest knob movement that is applied. Below this the reading is left
// pending, so a slow turn still lands while ADC noise cannot accumulate.
static constexpr float kKnobQuantum = 0.002f;

int SlotWithHysteresis(float knob, int num_slots, int current)
{
    const float pos = knob * static_cast<float>(num_slots);
    int         raw = static_cast<int>(pos);
    if(raw < 0)
        raw = 0;
    if(raw > num_slots - 1)
        raw = num_slots - 1;

    if(raw == current)
        return current;

    // The boundary we would be crossing, in slot units.
    const float boundary = (raw > current) ? static_cast<float>(current + 1)
                                           : static_cast<float>(current);
    if(fabsf(pos - boundary) < kHysteresis)
        return current;

    return raw;
}

void Ui::Init(DaisyPod* pod)
{
    pod_ = pod;
    SyncKnobs();
}

void Ui::SyncKnobs()
{
    // Take a fresh reference so a page change never applies stale movement.
    last_raw_[0] = pod_ ? pod_->GetKnobValue(DaisyPod::KNOB_1) : 0.0f;
    last_raw_[1] = pod_ ? pod_->GetKnobValue(DaisyPod::KNOB_2) : 0.0f;
}

void Ui::UpdateKnob(float raw, float* target, int index)
{
    const float delta = raw - last_raw_[index];
    if(fabsf(delta) < kKnobQuantum)
        return;
    last_raw_[index] = raw;
    *target          = daisysp::fclamp(*target + delta, 0.0f, 1.0f);
}

void Ui::Update()
{
    gestures_ = Gestures{};
    panic_    = false;
    if(!pod_)
        return;

    pod_->ProcessAllControls();
    tick_++;

    // --- Encoder turn: page select ---
    const int inc = pod_->encoder.Increment();
    if(inc != 0)
    {
        int p = static_cast<int>(page_) + inc;
        const int n = static_cast<int>(Page::Count);
        while(p < 0)
            p += n;
        p %= n;
        page_       = static_cast<Page>(p);
        page_flash_ = 1.0f;
        SyncKnobs();
    }

    const bool b1 = pod_->button1.Pressed();
    const bool b2 = pod_->button2.Pressed();

    // --- Both buttons: quick press stops all notes, 2 s hold clears the loop.
    // This stands in for the Touch 2's P10 + P11 clear hold. All-notes-off
    // moved here because the encoder click is now the looper button.
    if(b1 && b2)
    {
        if(!combo_active_)
        {
            combo_active_ = true;
            combo_fired_  = false;
        }
        // Neither button should also fire its own action on release.
        b1_consumed_ = true;
        b2_consumed_ = true;

        const float held
            = fminf(pod_->button1.TimeHeldMs(), pod_->button2.TimeHeldMs());
        clear_progress_ = fminf(held / 2000.0f, 1.0f);
        if(!combo_fired_ && held >= 2000.0f)
        {
            gestures_.clear = true;
            combo_fired_    = true;
        }
    }
    else
    {
        if(combo_active_)
        {
            // Released before the clear armed: treat it as all-notes-off.
            if(!combo_fired_)
            {
                gestures_.panic = true;
                panic_          = true;
            }
            combo_active_ = false;
        }
        clear_progress_ = 0.0f;
    }

    // --- Encoder click: the looper button, P01's stand-in ---
    if(pod_->encoder.RisingEdge())
    {
        enc_hold_fired_ = false;
        enc_deferred_   = false;

        if(b1)
        {
            gestures_.stop_resume = true; // P10 + P01
            b1_consumed_          = true;
        }
        else if(b2)
        {
            gestures_.remove_last = true; // P11 + P01
            b2_consumed_          = true;
        }
        else if(loop_state_ == LoopState::Playing)
        {
            // Only in PLAYING does a hold mean something, so the tap has to
            // wait for release to be told apart from it.
            enc_deferred_ = true;
        }
        else
        {
            gestures_.tap = true;
        }
    }
    if(enc_deferred_ && !enc_hold_fired_ && pod_->encoder.Pressed()
       && pod_->encoder.TimeHeldMs() >= 1500.0f)
    {
        gestures_.hold_mute = true;
        enc_hold_fired_     = true;
    }
    if(pod_->encoder.FallingEdge())
    {
        if(enc_deferred_ && !enc_hold_fired_)
            gestures_.tap = true;
        enc_deferred_ = false;
    }

    // --- Buttons alone: octave and interval, fired on release so a button
    // used as a modifier does not also change a musical setting.
    if(pod_->button1.FallingEdge())
    {
        if(!b1_consumed_)
        {
            // Switch B equivalent: octave shift, base -> up -> down -> base.
            if(params_.octave == OctaveShift::Base)
                params_.octave = OctaveShift::Up;
            else if(params_.octave == OctaveShift::Up)
                params_.octave = OctaveShift::Down;
            else
                params_.octave = OctaveShift::Base;
        }
        b1_consumed_ = false;
    }
    if(pod_->button2.FallingEdge())
    {
        if(!b2_consumed_)
        {
            // Switch A equivalent: interval doubling, root -> fifth -> third.
            if(params_.interval == IntervalMode::Root)
                params_.interval = IntervalMode::Fifth;
            else if(params_.interval == IntervalMode::Fifth)
                params_.interval = IntervalMode::Third;
            else
                params_.interval = IntervalMode::Root;
        }
        b2_consumed_ = false;
    }

    // --- Knobs, routed to whichever page is active ---
    const float k1 = pod_->GetKnobValue(DaisyPod::KNOB_1);
    const float k2 = pod_->GetKnobValue(DaisyPod::KNOB_2);

    switch(page_)
    {
        case Page::LingerTimbre:
            UpdateKnob(k1, &params_.linger, 0);
            UpdateKnob(k2, &params_.timbre, 1);
            break;
        case Page::VoiceScale:
            UpdateKnob(k1, &params_.voice, 0);
            UpdateKnob(k2, &params_.scale, 1);
            break;
        case Page::AmbientEcho:
            UpdateKnob(k1, &params_.ambient, 0);
            UpdateKnob(k2, &params_.echo, 1);
            break;
        case Page::ResonanceVolume:
            UpdateKnob(k1, &params_.resonance, 0);
            UpdateKnob(k2, &params_.volume, 1);
            break;
        default: break;
    }

    params_.voice_slot
        = SlotWithHysteresis(params_.voice, kNumVoices, params_.voice_slot);
    params_.scale_slot
        = SlotWithHysteresis(params_.scale, kNumScales, params_.scale_slot);

    visited_voices_ |= (1u << params_.voice_slot);
    visited_scales_ |= (1u << params_.scale_slot);

    UpdateLeds();
}

void Ui::UpdateLeds()
{
    // LED 1: page colour. Brightens briefly when the page changes.
    float pr = 0.0f, pg = 0.0f, pb = 0.0f;
    switch(page_)
    {
        case Page::LingerTimbre:    pr = 1.0f; break;                    // red
        case Page::VoiceScale:      pg = 1.0f; break;                    // green
        case Page::AmbientEcho:     pb = 1.0f; break;                    // blue
        case Page::ResonanceVolume: pr = 1.0f; pg = 0.6f; break;         // amber
        default: break;
    }

    // The looper takes over LED 1 whenever it has something to say. Per the
    // spec the LED only reports "am I capturing" and "ready to capture" -- the
    // loop itself is audible, so playback needs no visual cue.
    const bool blink10  = (tick_ % 100) < 50; // 10 Hz
    const bool strobe20 = (tick_ % 50) < 25;  // 20 Hz

    if(combo_active_ && clear_progress_ > 0.0f)
    {
        // Strobe previews the destructive clear for its whole 2 s arming.
        const float v = strobe20 ? 1.0f : 0.0f;
        pod_->led1.Set(v, v, v);
    }
    else if(loop_state_ == LoopState::Armed)
    {
        pod_->led1.Set(1.0f, 1.0f, 1.0f); // solid: waiting for the first note
    }
    else if(loop_state_ == LoopState::Recording
            || loop_state_ == LoopState::Overdubbing)
    {
        const float v = blink10 ? 1.0f : 0.0f;
        pod_->led1.Set(v, 0.0f, 0.0f);
    }
    else
    {
        const float base = 0.25f + 0.75f * page_flash_;
        pod_->led1.Set(pr * base, pg * base, pb * base);
    }

    // LED 2: octave state as colour, with a white flash on each note.
    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch(params_.octave)
    {
        case OctaveShift::Down: b = 0.5f; break;
        case OctaveShift::Base: g = 0.5f; break;
        case OctaveShift::Up:   r = 0.5f; break;
    }
    // Interval doubling adds a violet tint so both states read at a glance.
    if(params_.interval == IntervalMode::Fifth)
        b += 0.35f;
    else if(params_.interval == IntervalMode::Third)
    {
        r += 0.35f;
        b += 0.35f;
    }

    r = fminf(r + note_flash_, 1.0f);
    g = fminf(g + note_flash_, 1.0f);
    b = fminf(b + note_flash_, 1.0f);
    pod_->led2.Set(r, g, b);

    pod_->UpdateLeds();

    // Decay the transient flashes. Update() runs once per audio block, which
    // at a 48-sample block and 48 kHz is once a millisecond -- so these
    // coefficients are per-millisecond and put the note flash at roughly
    // 150 ms and the page flash at roughly 250 ms. Anything much faster is
    // too brief to catch while you are actually playing.
    note_flash_ *= 0.980f;
    page_flash_ *= 0.988f;
}

} // namespace zen
