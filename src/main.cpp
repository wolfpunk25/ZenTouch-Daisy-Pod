#include "daisy_pod.h"
#include "daisysp.h"
#include "zen_voice.h"
#include "zen_fx.h"
#include "zen_ui.h"
#include "zen_scales.h"
#include "zen_looper.h"
#include "util/CpuLoadMeter.h"
#include <cstdlib>

// Set to 1 to report knob positions and slot selections over USB serial. Off by
// default: it costs flash we are short of, and prints continuously while you
// play. Read it with `cat /dev/cu.usbmodem*` after `stty -f <port> 115200 raw`.
#define ZEN_DEBUG 0

using namespace daisy;
using namespace daisysp;
using namespace zen;

// ---------------------------------------------------------------------------
// ZenTouch for Daisy Pod
//
// A reimplementation of StubeMusic's ZenTouch for Synthux Touch 2, rebuilt for
// the Daisy Pod's control surface. The Touch 2's twelve capacitive tines become
// MIDI note input; its eight knobs become four encoder-selected pages of two.
// The synthesis, scales, and effects follow the published ZenTouch spec.
// ---------------------------------------------------------------------------

// The manual caps *live* polyphony at five, and says already-fading voices
// release their slot and stop counting against the cap. That only works if the
// physical pool is bigger than the cap, so a stolen voice can finish its 30 ms
// fade in its own slot while the note that displaced it starts immediately.
static constexpr int kLiveVoices = 5; // cap on non-fading voices
static constexpr int kVoicePool  = 8; // physical slots, including fade-outs
static constexpr int kRootNote  = 48; // C3, the Touch 2's tine-0 root

// DaisySP's String clamps its delay line to kDelayLineSize - 4 = 1020 samples,
// which puts its lowest true pitch at 48000/1020 ~= 47Hz, just under MIDI 31.
// Below that the clamp holds the pitch flat, so every low note sounds the same
// rather than descending. Octave-down makes that reachable from MIDI 43 on an
// ordinary keyboard, so notes are folded up into range instead.
static constexpr int kMinPlayableNote = 33;

static DaisyPod pod;
static Voice    voices[kVoicePool];
static Fx       fx;
static Ui       ui;
static zen::Looper looper;
static CpuLoadMeter cpu_meter;

// Main loop parses MIDI; the audio callback owns the voices. A tiny
// single-producer/single-consumer queue hands notes across so the two never
// touch the same voice state.
struct NoteEvent
{
    uint8_t note;
    uint8_t velocity;
};
static constexpr int      kQueueSize = 32;
static volatile NoteEvent note_queue[kQueueSize];
static volatile uint32_t  q_write = 0;
static volatile uint32_t  q_read  = 0;

static void PushNote(uint8_t note, uint8_t velocity)
{
    const uint32_t next = (q_write + 1) % kQueueSize;
    if(next == q_read)
        return; // full; drop rather than block the MIDI parser
    note_queue[q_write].note     = note;
    note_queue[q_write].velocity = velocity;
    q_write                      = next;
}

// --- Voice allocation -------------------------------------------------------

// Find the oldest voice matching a predicate, or -1.
template <typename Pred>
static int OldestVoice(Pred keep)
{
    int      idx = -1;
    uint32_t age = 0;
    for(int i = 0; i < kVoicePool; i++)
    {
        if(!keep(voices[i]))
            continue;
        if(idx < 0 || voices[i].Age() > age)
        {
            age = voices[i].Age();
            idx = i;
        }
    }
    return idx;
}

// Always returns a voice to play into. If the live cap is already reached, the
// oldest live voice is sent into its fade first -- it keeps sounding in its own
// slot while the new note starts cleanly in another one.
static Voice* AllocateVoice()
{
    int live = 0;
    for(int i = 0; i < kVoicePool; i++)
        if(voices[i].IsActive() && !voices[i].IsFading())
            live++;

    if(live >= kLiveVoices)
    {
        const int steal
            = OldestVoice([](const Voice& v) { return v.IsActive() && !v.IsFading(); });
        if(steal >= 0)
            voices[steal].FadeOut();
    }

    for(int i = 0; i < kVoicePool; i++)
        if(!voices[i].IsActive())
            return &voices[i];

    // Every physical slot is busy. Prefer one that is already fading, since it
    // is on its way out anyway -- otherwise we could steal back the very voice
    // we just sent into its fade above and cut it abruptly. NoteOn resets the
    // string either way, so the worst case is a small click, not a lost note.
    int idx = OldestVoice([](const Voice& v) { return v.IsFading(); });
    if(idx < 0)
        idx = OldestVoice([](const Voice& v) { return v.IsActive(); });
    return (idx >= 0) ? &voices[idx] : &voices[0];
}

// Each NoteOn gets +/-8 percent trigger variation so repeated taps never feel
// mechanically identical.
static float HumanizedAmp()
{
    const float mid   = (kTrigAmpMin + kTrigAmpMax) * 0.5f;
    const float range = (kTrigAmpMax - kTrigAmpMin) * 0.5f;
    const float r     = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
    return mid + range * (r * 2.0f - 1.0f);
}

// Snap an incoming note onto the active scale, so a full keyboard stays in
// mode instead of only nine keys being useful.
static int QuantizeToScale(int note, const Scale& s)
{
    const uint16_t mask = PitchClassMask(s);
    const int      pc   = (((note - kRootNote) % 12) + 12) % 12;

    for(int d = 0; d <= 6; d++)
    {
        if((mask >> ((pc + d) % 12)) & 1u)
            return note + d;
        if((mask >> ((((pc - d) % 12) + 12) % 12)) & 1u)
            return note - d;
    }
    return note;
}

static void TriggerNote(int midi_note, float amp)
{
    const Params& p = ui.params();
    const Scale&  s = kScales[p.scale_slot];

    int note = QuantizeToScale(midi_note, s);
    note += static_cast<int>(p.octave) * 12;
    while(note < kMinPlayableNote)
        note += 12;

    Voice* v = AllocateVoice();
    if(v)
        v->NoteOn(mtof(static_cast<float>(note)),
                  amp,
                  p.voice_slot,
                  p.timbre,
                  p.linger);

    // Interval doubling takes a second voice, exactly as on the Touch 2.
    if(p.interval != IntervalMode::Root)
    {
        const int semis = (p.interval == IntervalMode::Fifth)
                              ? kFifthInterval
                              : ThirdInterval(s, note - kRootNote);
        Voice* v2 = AllocateVoice();
        if(v2)
            v2->NoteOn(mtof(static_cast<float>(note + semis)),
                       amp,
                       p.voice_slot,
                       p.timbre,
                       p.linger);
    }

    looper.NotePlayed();
    ui.NoteFlash();
}

// --- Audio ------------------------------------------------------------------

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    cpu_meter.OnBlockStart();

    ui.Update();

    const Gestures& g = ui.gestures();
    if(g.tap)
        looper.Tap();
    if(g.hold_mute)
        looper.ToggleMute();
    if(g.stop_resume)
        looper.StopResume();
    if(g.remove_last)
        looper.RemoveLast();
    if(g.clear)
        looper.Clear();
    if(g.panic)
        for(int i = 0; i < kVoicePool; i++)
            voices[i].FadeOut();

    ui.SetLooperState(looper.State(), looper.Overdubs());

    // Drain any notes the MIDI parser queued since the last block.
    while(q_read != q_write)
    {
        const NoteEvent e = {note_queue[q_read].note, note_queue[q_read].velocity};
        q_read            = (q_read + 1) % kQueueSize;
        const float vel   = static_cast<float>(e.velocity) / 127.0f;
        TriggerNote(e.note, HumanizedAmp() * (0.4f + 0.6f * vel));
    }

    const Params& p = ui.params();
    fx.SetResonance(p.resonance);
    fx.SetAmbient(p.ambient);
    fx.SetEcho(p.echo);
    fx.SetVolume(p.volume);

    for(size_t i = 0; i < size; i++)
    {
        float dry = 0.0f;
        for(int v = 0; v < kVoicePool; v++)
            dry += voices[v].Process();
        dry *= 0.4f; // headroom for five voices before the FX bus

        // The looper records this live mix and returns its own playback. Both
        // go into the FX bus together, so Resonance, Ambient and Echo re-apply
        // to the loop live while everything baked into the recording -- voice,
        // scale, timbre, linger, octave, interval -- travels with the audio.
        const float loop = looper.Process(dry);

        float l, r;
        fx.Process(dry + loop, in[0][i], in[1][i], &l, &r);
        out[0][i] = l;
        out[1][i] = r;
    }

    cpu_meter.OnBlockEnd();
}

// --- MIDI -------------------------------------------------------------------

static void HandleMidi(MidiEvent m)
{
    switch(m.type)
    {
        case NoteOn:
        {
            NoteOnEvent e = m.AsNoteOn();
            // A kalimba tine has no note-off: notes ring until they decay, so
            // a zero-velocity NoteOn is simply ignored rather than silencing.
            if(e.velocity > 0)
                PushNote(e.note, e.velocity);
            break;
        }
        case ControlChange:
        {
            ControlChangeEvent e = m.AsControlChange();
            if(e.control_number == 123) // All Notes Off
                for(int i = 0; i < kVoicePool; i++)
                    voices[i].FadeOut();
            break;
        }
        default: break;
    }
}

// --- Entry ------------------------------------------------------------------

int main(void)
{
    pod.Init();
    pod.SetAudioBlockSize(48);
#if ZEN_DEBUG
    pod.seed.StartLog(false);
#endif

    const float sr = pod.AudioSampleRate();

    for(int i = 0; i < kVoicePool; i++)
        voices[i].Init(sr);
    fx.Init(sr);
    ui.Init(&pod);
    looper.Init();

    cpu_meter.Init(pod.AudioSampleRate(), pod.AudioBlockSize());

    pod.StartAdc();
    pod.StartAudio(AudioCallback);
    pod.midi.StartReceive();

#if ZEN_DEBUG
    int      last_voice = -1, last_scale = -1, last_k1 = -1;
    uint32_t last_report = 0;
#endif

    while(1)
    {
        pod.midi.Listen();
        while(pod.midi.HasEvents())
            HandleMidi(pod.midi.PopEvent());

#if ZEN_DEBUG
        // Sample on a timer rather than on change, and report the raw knob
        // reading, so a slot that is never selected can be told apart from one
        // the logger merely missed.
        const uint32_t now = System::GetNow();
        if(now - last_report >= 200)
        {
            last_report      = now;
            const Params& p  = ui.params();
            const int     k1 = static_cast<int>(pod.GetKnobValue(DaisyPod::KNOB_1) * 1000.0f);
            if(true)
            {
                last_voice = p.voice_slot;
                last_scale = p.scale_slot;
                last_k1    = k1;
                pod.seed.PrintLine(
                    "k1=%d | voice %d %s | scale %d | seen v=%x s=%x | t60ms %d | cpu avg %d max %d %%",
                    k1, p.voice_slot, kVoices[p.voice_slot].name, p.scale_slot,
                    static_cast<int>(ui.VisitedVoices()),
                    static_cast<int>(ui.VisitedScales()),
                    static_cast<int>(MapLingerT60(p.linger,
                                                  kVoices[p.voice_slot].decay_s)
                                     * 1000.0f),
                    static_cast<int>(cpu_meter.GetAvgCpuLoad() * 100.0f),
                    static_cast<int>(cpu_meter.GetMaxCpuLoad() * 100.0f));
            }
        }
#endif
    }
}
