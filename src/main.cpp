#include "daisy_pod.h"
#include "daisysp.h"
#include "zen_voice.h"
#include "zen_fx.h"
#include "zen_ui.h"
#include "zen_scales.h"
#include <cstdlib>

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

static constexpr int kMaxVoices = 5; // manual: up to five simultaneous voices
static constexpr int kRootNote  = 48; // C3, the Touch 2's tine-0 root

static DaisyPod pod;
static Voice    voices[kMaxVoices];
static Fx       fx;
static Ui       ui;

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

// Prefer a free slot; otherwise fade the oldest voice that is not already on
// its way out. Voices that are already fading have effectively released their
// slot and do not count against the cap.
static Voice* AllocateVoice()
{
    for(int i = 0; i < kMaxVoices; i++)
        if(!voices[i].IsActive())
            return &voices[i];

    int      oldest_idx = -1;
    uint32_t oldest_age = 0;
    for(int i = 0; i < kMaxVoices; i++)
    {
        if(voices[i].IsFading())
            continue;
        if(voices[i].Age() >= oldest_age)
        {
            oldest_age = voices[i].Age();
            oldest_idx = i;
        }
    }

    if(oldest_idx < 0)
        return nullptr; // everything is already fading; let them finish

    voices[oldest_idx].FadeOut();
    return nullptr; // the slot frees up once the fade lands
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

    ui.NoteFlash();
}

// --- Audio ------------------------------------------------------------------

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    ui.Update();

    if(ui.PanicRequested())
        for(int i = 0; i < kMaxVoices; i++)
            voices[i].FadeOut();

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
        for(int v = 0; v < kMaxVoices; v++)
            dry += voices[v].Process();
        dry *= 0.4f; // headroom for five voices before the FX bus

        float l, r;
        fx.Process(dry, in[0][i], in[1][i], &l, &r);
        out[0][i] = l;
        out[1][i] = r;
    }
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
                for(int i = 0; i < kMaxVoices; i++)
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
    pod.seed.StartLog(false);

    const float sr = pod.AudioSampleRate();

    for(int i = 0; i < kMaxVoices; i++)
        voices[i].Init(sr);
    fx.Init(sr);
    ui.Init(&pod);

    pod.StartAdc();
    pod.StartAudio(AudioCallback);
    pod.midi.StartReceive();

    while(1)
    {
        pod.midi.Listen();
        while(pod.midi.HasEvents())
            HandleMidi(pod.midi.PopEvent());
    }
}
