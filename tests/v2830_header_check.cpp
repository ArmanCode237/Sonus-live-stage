#include "../src/dsp/StageSlotFxRack.h"
int main(){ SonusDSP::StageAtmosphericShimmer s; s.init(48000.f); float a=.1f,b=-.2f; s.processStereo(a,b); return 0; }
