#include "zen_looper.h"
#include <cstdio>
#include <cmath>
using namespace zen;

static Looper lp;
static int fails = 0;

static const char* N(LoopState s){
    switch(s){case LoopState::Idle:return "Idle";case LoopState::Armed:return "Armed";
    case LoopState::Recording:return "Recording";case LoopState::Playing:return "Playing";
    default:return "Overdubbing";}
}
static void CHK(const char* what, bool ok){
    printf("  %-46s %s\n", what, ok?"ok":"FAIL");
    if(!ok) fails++;
}
// run n samples of input, return the summed absolute playback
static float Run(long n, float in){
    float acc = 0.0f;
    for(long i=0;i<n;i++) acc += fabsf(lp.Process(in));
    return acc;
}

int main()
{
    lp.Init();
    printf("state machine\n");
    CHK("starts Idle with no data", lp.State()==LoopState::Idle && !lp.HasData());

    lp.Tap();
    CHK("tap -> Armed", lp.State()==LoopState::Armed);
    Run(1000, 0.5f);
    CHK("armed does not start the clock", lp.Length()==0);

    lp.NotePlayed();
    CHK("first note -> Recording", lp.State()==LoopState::Recording);
    Run(4800, 0.5f);                     // 100 ms
    lp.Tap();
    CHK("tap closes -> Playing", lp.State()==LoopState::Playing);
    CHK("loop length is what was captured", lp.Length()==4800);

    float base = Run(4800, 0.0f);
    CHK("base layer plays back audio", base > 100.0f);

    printf("overdub\n");
    lp.Tap();
    CHK("tap in Playing -> Overdubbing", lp.State()==LoopState::Overdubbing);
    Run(4800, 0.25f);                    // one full revolution, writes
    lp.Tap();
    CHK("tap commits -> Playing", lp.State()==LoopState::Playing);
    CHK("one overdub committed", lp.Overdubs()==1);
    float withDub = Run(4800, 0.0f);
    CHK("overdub is audible in playback", withDub > base * 1.2f);

    printf("mute / remove\n");
    lp.ToggleMute();
    CHK("mute flags last overdub", lp.LastMuted());
    float muted = Run(4800, 0.0f);
    CHK("muted playback drops back toward base", muted < withDub * 0.95f);
    lp.ToggleMute();
    CHK("unmute restores", !lp.LastMuted());

    lp.RemoveLast();
    CHK("remove last -> 0 overdubs", lp.Overdubs()==0);
    CHK("base survives removal", lp.HasData());

    printf("empty overdub is discarded\n");
    lp.Tap(); Run(4800, 0.0f); lp.Tap();
    CHK("silent overdub not committed", lp.Overdubs()==0);

    printf("stop / resume / clear\n");
    lp.StopResume();
    CHK("park -> Idle but keeps data", lp.State()==LoopState::Idle && lp.HasData());
    CHK("parked loop is silent", Run(4800,0.0f)==0.0f);
    lp.StopResume();
    CHK("resume -> Playing", lp.State()==LoopState::Playing);

    lp.Clear();
    CHK("clear wipes to Idle no data", lp.State()==LoopState::Idle && !lp.HasData());

    printf("documented gotcha\n");
    lp.Tap(); lp.NotePlayed(); Run(4800,0.5f); lp.Tap();
    CHK("recorded again", lp.HasData() && lp.State()==LoopState::Playing);
    lp.StopResume();                      // park it
    lp.Tap();                             // plain tap on a parked loop
    CHK("plain tap on parked loop wipes and arms", lp.State()==LoopState::Armed && !lp.HasData());

    printf("overdub ceiling\n");
    lp.Tap(); CHK("cancel arm", lp.State()==LoopState::Idle);
    lp.Tap(); lp.NotePlayed(); Run(4800,0.5f); lp.Tap();
    for(int i=0;i<4;i++){ lp.Tap(); Run(4800,0.2f); lp.Tap(); }
    CHK("stops at 3 overdubs", lp.Overdubs()==3);

    printf("overdub starting mid-loop\n");
    // An overdub begins wherever playback happens to be. If the take's first
    // pass is tracked by loop wraps rather than from that point, everything
    // between the loop start and it gets summed onto whatever the layer held
    // before -- stale audio, or uninitialised SDRAM as static.
    lp.Clear();
    lp.Tap(); lp.NotePlayed(); Run(4800, 0.5f); lp.Tap();   // base = 0.5
    CHK("base recorded", lp.Length()==4800 && lp.State()==LoopState::Playing);

    lp.Tap(); Run(4800, 0.25f); lp.Tap();                   // take A into layer 1
    CHK("take A committed", lp.Overdubs()==1);
    lp.RemoveLast();
    CHK("take A removed, layer still holds its audio", lp.Overdubs()==0);

    Run(2000, 0.0f);                                        // park playback mid-loop
    lp.Tap();                                               // take B starts at 2000
    CHK("take B overdubbing", lp.State()==LoopState::Overdubbing);
    Run(4800, 0.10f);                                       // exactly one pass
    lp.Tap();
    CHK("take B committed", lp.Overdubs()==1);

    // One full revolution of playback should be base + take B everywhere.
    // If take A leaked back in, part of the loop reads higher.
    float lo = 1e9f, hi = -1e9f;
    for(int i = 0; i < 4800; i++) { float v = lp.Process(0.0f); if(v<lo) lo=v; if(v>hi) hi=v; }
    CHK("playback is uniform across the loop", fabsf(hi-lo) < 0.02f);
    CHK("removed take A did not leak back in", hi < 0.70f);

    printf("commit before the pass completes\n");
    lp.Clear();
    lp.Tap(); lp.NotePlayed(); Run(4800, 0.5f); lp.Tap();
    lp.Tap(); Run(4800, 0.30f); lp.Tap();                   // full take
    lp.RemoveLast();
    lp.Tap(); Run(1000, 0.20f); lp.Tap();                   // commit after 1000 of 4800
    lo = 1e9f; hi = -1e9f;
    for(int i = 0; i < 4800; i++) { float v = lp.Process(0.0f); if(v<lo) lo=v; if(v>hi) hi=v; }
    CHK("uncovered remainder cleared, not left holding old audio", hi < 0.80f);

    printf("\n%s\n", fails? "SOME CHECKS FAILED" : "all checks passed");
    return fails;
}
