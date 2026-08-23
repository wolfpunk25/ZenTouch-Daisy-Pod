#include "zen_ui.h"
#include <cmath>

using namespace daisy;

namespace zen
{
// Margin, in slot widths, that the knob must travel past a boundary before the
// slot changes. Keeps noon from rattling between two voices.
static constexpr float kHysteresis = 0.15f;

// How close the knob must come to the stored value before it takes control.
static constexpr float kCatchWindow = 0.02f;

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
    ResetCatch();
}

void Ui::ResetCatch()
{
    caught_[0]   = false;
    caught_[1]   = false;
    last_raw_[0] = pod_ ? pod_->GetKnobValue(DaisyPod::KNOB_1) : 0.0f;
    last_raw_[1] = pod_ ? pod_->GetKnobValue(DaisyPod::KNOB_2) : 0.0f;
}

void Ui::UpdateKnob(float raw, float* target, int index)
{
    if(!caught_[index])
    {
        const float stored = *target;
        const float prev   = last_raw_[index];
        const bool  near   = fabsf(raw - stored) < kCatchWindow;
        const bool  crossed
            = (prev < stored && raw >= stored) || (prev > stored && raw <= stored);

        last_raw_[index] = raw;
        if(!near && !crossed)
            return; // knob is somewhere else entirely; ignore it

        caught_[index] = true;
    }

    last_raw_[index] = raw;
    *target          = raw;
}

void Ui::Update()
{
    panic_ = false;
    if(!pod_)
        return;

    pod_->ProcessAllControls();

    // --- Encoder: page select, click for all-notes-off ---
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
        ResetCatch();
    }
    if(pod_->encoder.RisingEdge())
        panic_ = true;

    // --- Buttons: the two Touch 2 toggle switches, cycled instead ---
    if(pod_->button1.RisingEdge())
    {
        // Switch B equivalent: octave shift, base -> up -> down -> base.
        if(params_.octave == OctaveShift::Base)
            params_.octave = OctaveShift::Up;
        else if(params_.octave == OctaveShift::Up)
            params_.octave = OctaveShift::Down;
        else
            params_.octave = OctaveShift::Base;
    }
    if(pod_->button2.RisingEdge())
    {
        // Switch A equivalent: interval doubling, root -> fifth -> third.
        if(params_.interval == IntervalMode::Root)
            params_.interval = IntervalMode::Fifth;
        else if(params_.interval == IntervalMode::Fifth)
            params_.interval = IntervalMode::Third;
        else
            params_.interval = IntervalMode::Root;
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

    const float base = 0.25f + 0.75f * page_flash_;
    pod_->led1.Set(pr * base, pg * base, pb * base);

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

    // Decay the transient flashes. Update() runs once per audio block.
    note_flash_ *= 0.85f;
    page_flash_ *= 0.90f;
}

} // namespace zen
