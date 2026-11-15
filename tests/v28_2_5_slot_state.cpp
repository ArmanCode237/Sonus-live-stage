// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// Check v3 slot storage, control API field mapping, and independent rack state.
#include "../src/modules/NordPianoEngine.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace NordDSP;
int main(){
 NordPianoEngine host;
 host.setStudioSlot(0,0,0,5); // Delay Piano
 host.setStudioSlot(3,0,0,5); // Delay Master
 host.setStudioSlot(0,0,10,.75f); // p8 local BPM ~232.5
 host.setStudioSlot(3,0,10,.25f); // p8 local BPM 97.5
 host.setStudioSlot(0,1,0,6); // new Shimmer engine v3
 for(int p=16;p<=20;p++)assert(host.setStudioSlot(0,1,p+3,.13f+float(p-16)*.12f));
 auto piano=host.getStudioRack(0),master=host.getStudioRack(3);
 assert(piano.slots[0].version==3&&piano.slots[1].version==3);
 assert(std::abs(piano.slots[0].p[8]-.75f)<.0001f);
 assert(std::abs(master.slots[0].p[8]-.25f)<.0001f);
 assert(std::abs(piano.slots[1].p[16]-.13f)<.0001f);
 assert(std::abs(piano.slots[1].p[20]-.61f)<.0001f);
 host.setStudioRack(2,piano); // Copy to unrelated Organ rack
 assert(std::abs(host.getStudioRack(2).slots[1].p[20]-.61f)<.0001f);
 assert(std::abs(host.getStudioRack(3).slots[0].p[8]-.25f)<.0001f);
 assert(!host.setStudioSlot(0,1,27,.7f));
 std::cout<<"PASS v28.2.5 independent slot state and v3 field mapping\n";
}
