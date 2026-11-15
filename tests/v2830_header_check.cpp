#include "../src/dsp/NordSlotFxRack.h"
int main(){ NordDSP::NordAtmosphericShimmer s; s.init(48000.f); float a=.1f,b=-.2f; s.processStereo(a,b); return 0; }
