#include "zen_voice.h"
#include "zen_voices.h"
#include <cstdio>
#include <cmath>
using namespace zen;

int main()
{
    const float sr = 48000.0f;
    printf("%-10s %8s %8s %9s %s\n", "voice", "T60(s)", "peak", "centroid", "character");
    for(int slot = 0; slot < kNumVoices; slot++)
    {
        Voice v; v.Init(sr);
        v.NoteOn(130.81f, 0.70f, slot, 0.5f, 0.5f); // both knobs at noon

        const long n = (long)(6.0f * sr);
        float peak = 0.0f, env = 0.0f; long t60 = -1;
        // crude spectral centroid via zero-crossing rate over the first second
        long zc = 0; float prev = 0.0f; long zc_n = 0;
        for(long i = 0; i < n; i++)
        {
            float out = v.Process();
            float a = fabsf(out);
            if(a > peak) peak = a;
            env += (a - env) * 0.001f;
            if(i > sr*0.05f && t60 < 0 && env > 0 && env < peak*0.001f) t60 = i;
            if(i < sr) { if((out > 0) != (prev > 0)) zc++; zc_n++; prev = out; }
        }
        const char* ch = "-";
        switch(kVoices[slot].character) {
            case Character::PitchDip: ch="pitch dip"; break;
            case Character::AttackSwell: ch="attack swell"; break;
            case Character::Tremolo: ch="tremolo"; break;
            case Character::Vibrato: ch="vibrato"; break;
            case Character::Shimmer: ch="shimmer"; break;
            default: break;
        }
        printf("%-10s %8s %8.3f %9.0f %s\n", kVoices[slot].name,
            t60<0?"  >6.0":([](float x){static char b[12];snprintf(b,12,"%6.2f",x);return b;})((float)t60/sr),
            peak, (double)zc/2.0, ch);
    }
    return 0;
}
