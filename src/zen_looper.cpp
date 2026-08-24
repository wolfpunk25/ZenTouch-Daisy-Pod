#include "zen_looper.h"
#include "zen_util.h"
#include "dev/sdram.h"
#include "daisysp.h"

using namespace daisysp;

namespace zen
{
// ~23 MB. The Seed has 64 MB of SDRAM and the rest of the firmware uses under
// a megabyte of it, so this is comfortable.
static int16_t DSY_SDRAM_BSS loop_buf[kMaxLayers][kLoopMaxSamples];

static inline int16_t ToS16(float x)
{
    x = fclamp(x, -1.0f, 1.0f);
    return static_cast<int16_t>(x * 32767.0f);
}

static inline float FromS16(int16_t v)
{
    return static_cast<float>(v) * (1.0f / 32768.0f);
}

static inline int16_t AddS16(int16_t a, int16_t b)
{
    int32_t sum = static_cast<int32_t>(a) + static_cast<int32_t>(b);
    if(sum > 32767)
        sum = 32767;
    if(sum < -32768)
        sum = -32768;
    return static_cast<int16_t>(sum);
}

void Looper::ZeroSpan(int layer, size_t from, size_t to)
{
    if(length_ == 0)
        return;
    size_t i = from;
    while(i != to)
    {
        loop_buf[layer][i] = 0;
        if(++i >= length_)
            i = 0;
    }
}

void Looper::Init()
{
    // SDRAM comes up holding whatever was there before -- the startup code does
    // not clear it, and 23 MB would be far too slow to clear in the callback.
    // Doing it once here means an unwritten sample is silence, not noise.
    for(int layer = 0; layer < kMaxLayers; layer++)
        for(size_t i = 0; i < kLoopMaxSamples; i++)
            loop_buf[layer][i] = 0;

    state_     = LoopState::Idle;
    pos_       = 0;
    length_    = 0;
    overdubs_  = 0;
    mute_last_ = false;
}

void Looper::NotePlayed()
{
    // Arming does not start the clock; the first note does, so you can take as
    // long as you like between arming and playing.
    if(state_ == LoopState::Armed)
    {
        state_ = LoopState::Recording;
        pos_   = 0;
    }
}

void Looper::CloseRecording()
{
    if(pos_ == 0)
    {
        // Closed before anything was captured.
        state_  = LoopState::Idle;
        length_ = 0;
        return;
    }
    length_ = pos_;
    pos_    = 0;
    state_  = LoopState::Playing;
}

void Looper::CommitOverdub()
{
    const int layer = overdubs_ + 1;

    // Committed before the take had come all the way round: whatever it did not
    // reach still holds the previous take's audio, so clear it rather than let
    // it play back as part of this layer.
    if(overdub_first_pass_)
        ZeroSpan(layer, pos_, overdub_start_pos_);

    if(overdub_had_audio_)
        overdubs_++;
    state_ = LoopState::Playing;
}

void Looper::Tap()
{
    switch(state_)
    {
        case LoopState::Idle:
            // Note the spec's gotcha: a plain tap on a parked loop wipes it and
            // arms an empty one. Resuming without losing it is StopResume().
            if(HasData())
                Clear();
            state_ = LoopState::Armed;
            break;

        case LoopState::Armed: // cancel
            state_ = LoopState::Idle;
            break;

        case LoopState::Recording: CloseRecording(); break;

        case LoopState::Playing:
            if(overdubs_ >= kMaxOverdubs)
                break; // no-op once all three overdubs exist
            state_              = LoopState::Overdubbing;
            overdub_start_pos_  = pos_;
            overdub_first_pass_ = true;
            overdub_had_audio_  = false;
            break;

        case LoopState::Overdubbing: CommitOverdub(); break;
    }
}

void Looper::ToggleMute()
{
    // Non-destructive: the layer's audio stays put, the playback sum just skips
    // it, so unmuting brings it back instantly mid-revolution.
    if(state_ == LoopState::Playing && overdubs_ > 0)
        mute_last_ = !mute_last_;
}

void Looper::StopResume()
{
    switch(state_)
    {
        case LoopState::Playing:
        case LoopState::Overdubbing:
            state_ = LoopState::Idle; // parked, data retained
            pos_   = 0;
            break;

        case LoopState::Idle:
            if(HasData())
            {
                state_ = LoopState::Playing; // resume from the downbeat
                pos_   = 0;
            }
            break;

        case LoopState::Armed:
        case LoopState::Recording:
            state_  = LoopState::Idle; // cancel, nothing captured worth keeping
            length_ = 0;
            pos_    = 0;
            break;
    }
}

void Looper::RemoveLast()
{
    if(state_ != LoopState::Playing || overdubs_ == 0)
        return;
    overdubs_--;
    mute_last_ = false;
}

void Looper::Clear()
{
    state_     = LoopState::Idle;
    pos_       = 0;
    length_    = 0;
    overdubs_  = 0;
    mute_last_ = false;
}

float Looper::Process(float live_dry)
{
    switch(state_)
    {
        case LoopState::Recording:
        {
            loop_buf[0][pos_] = ToS16(live_dry);
            pos_++;
            if(pos_ >= kLoopMaxSamples)
            {
                // Hit the 60 s ceiling: close silently, length pegged to the
                // buffer, exactly as the spec describes.
                length_ = kLoopMaxSamples;
                pos_    = 0;
                state_  = LoopState::Playing;
            }
            return 0.0f;
        }

        case LoopState::Playing:
        case LoopState::Overdubbing:
        {
            float out = 0.0f;

            // Committed layers: base plus each committed overdub.
            for(int layer = 0; layer <= overdubs_; layer++)
            {
                if(layer > 0 && layer == overdubs_ && mute_last_)
                    continue;
                out += FromS16(loop_buf[layer][pos_]);
            }

            if(state_ == LoopState::Overdubbing)
            {
                const int layer = overdubs_ + 1;
                if(overdub_first_pass_)
                {
                    // First time round, overwrite whatever this layer held from
                    // a previous take. Nothing has been captured at this
                    // position yet, so there is nothing to monitor.
                    loop_buf[layer][pos_] = ToS16(live_dry);
                }
                else
                {
                    // Later revolutions stack, and what is already there is
                    // monitored so you hear the layer accumulating.
                    out += FromS16(loop_buf[layer][pos_]);
                    loop_buf[layer][pos_]
                        = AddS16(loop_buf[layer][pos_], ToS16(live_dry));
                }
                if(live_dry > 0.0005f || live_dry < -0.0005f)
                    overdub_had_audio_ = true;
            }

            pos_++;
            if(pos_ >= length_)
                pos_ = 0;

            // The first pass ends when the take arrives back where it began.
            if(state_ == LoopState::Overdubbing && overdub_first_pass_
               && pos_ == overdub_start_pos_)
                overdub_first_pass_ = false;

            return Sanitize(out);
        }

        case LoopState::Idle:
        case LoopState::Armed:
        default: return 0.0f;
    }
}

} // namespace zen
