# Host-side tests. The DSP and the looper state machine are ordinary portable
# C++, so they can be compiled for the development machine and checked without
# a Daisy attached. This is how the decay bug and the voice-distinctness
# question were both settled -- measure here first, flash second.
#
#   make -C tests        build and run everything
#   make -C tests looper just the state machine checks

DSP  = ../DaisySP/Source
SRC  = ../src
INC  = -Ishim -I$(SRC) -I$(DSP) -I$(DSP)/Utility -I$(DSP)/Filters \
       -I$(DSP)/PhysicalModeling -I$(DSP)/Dynamics -I$(DSP)/Synthesis \
       -I$(DSP)/Noise -I$(DSP)/Control -I$(DSP)/Effects -I$(DSP)/Drums
CXXFLAGS = -O1 -std=gnu++14 -Wall $(INC)

DSPSRC = $(DSP)/PhysicalModeling/KarplusString.cpp $(DSP)/Utility/dcblock.cpp \
         $(DSP)/Dynamics/crossfade.cpp $(DSP)/Filters/svf.cpp \
         $(DSP)/Synthesis/oscillator.cpp

.PHONY: all looper voices clean
all: looper voices

looper: build/looper_test
	@./build/looper_test

voices: build/voices_probe
	@./build/voices_probe

build/looper_test: looper_test.cpp $(SRC)/zen_looper.cpp | build
	@$(CXX) $(CXXFLAGS) $^ -o $@

build/voices_probe: voices_probe.cpp $(SRC)/zen_voice.cpp $(DSPSRC) | build
	@$(CXX) $(CXXFLAGS) $^ -o $@

build:
	@mkdir -p build

clean:
	@rm -rf build
