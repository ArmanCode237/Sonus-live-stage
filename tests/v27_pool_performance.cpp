// Full NPNo benchmark. Requires a user-supplied .npno file; no bank is bundled.
// Reports elapsed time to PLAYABLE (all PCM frames ready), distinct from disk
// cache persistence, which is asynchronous and flushed on pool destruction.
#include "../src/loaders/InstrumentPool.h"
#include "../src/loaders/BankLoader.h"
#include <chrono>
#include <iostream>
#include <string>
#include <cstdint>
using namespace SonusDSP;
using Clock=std::chrono::steady_clock;
static long ms(Clock::time_point t){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-t).count();}
static uint64_t checksum(const PianoInstrument& bank){
 uint64_t h=14695981039346656037ull;
 auto mix=[&](uint32_t n){h^=n;h*=1099511628211ull;};
 for(const auto& s:bank.strokes){
   mix(s.frames);mix(s.loop.start);mix(s.loop.length);
   for(int16_t x:s.pcmL)mix(uint16_t(x));
   for(int16_t x:s.pcmR)mix(uint16_t(x));
 }
 return h;
}
int main(int argc,char**argv){
 if(argc<2){std::cerr<<"Uso: LiveStageNpnoBench '/ruta/al/piano.npno'\n";return 2;}
 uint64_t hash=0;
 {
   InstrumentPool pool;std::string status;
   const auto t=Clock::now();auto bank=pool.getOrLoad(argv[1],status);
   const auto loadMs=ms(t);
   if(!bank){std::cerr<<status<<'\n';return 1;}
   for(const auto& s:bank->strokes)
     if(!s.readyFrames || s.readyFrames->load(std::memory_order_acquire)!=s.frames){std::cerr<<"Incomplete bank\n";return 3;}
   hash=checksum(*bank);
   std::cout<<"playable_ms="<<loadMs<<" strokes="<<bank->strokes.size()<<" hash="<<std::hex<<hash<<std::dec<<" "<<status<<'\n';
   const auto t2=Clock::now();const auto reused=pool.getOrLoad(argv[1],status);
   std::cout<<"RAM_recall_ms="<<ms(t2)<<" same_bank="<<(reused==bank)<<'\n';
 }
 {
   InstrumentPool pool;std::string status;
   const auto t=Clock::now();auto bank=pool.getOrLoad(argv[1],status);
   if(!bank){std::cerr<<status<<'\n';return 4;}
   const auto hash2=checksum(*bank);
   std::cout<<"cache_load_ms="<<ms(t)<<" hash_match="<<(hash2==hash)<<" status="<<status<<'\n';
   if(hash2!=hash)return 5;
 }
}
