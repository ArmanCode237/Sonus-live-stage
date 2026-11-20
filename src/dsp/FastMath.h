#pragma once
// v32.4 fast sine for the synth voice loop: a 4096-point table with linear interpolation
// (max error ~3e-7, far below audibility) instead of std::sin per sample, per voice and
// per FM operator. The table is built once (initFastMath() from StageSynthModule::init),
// so the audio thread never pays for the static initialisation.
#include <cmath>

namespace SonusDSP {

namespace fastmath_detail {
constexpr int kSineSize = 4096;
struct SineTable {
    float v[kSineSize + 1];
    SineTable() { for (int i = 0; i <= kSineSize; ++i) v[i] = float(std::sin(6.283185307179586 * double(i) / kSineSize)); }
};
inline const SineTable& table() { static const SineTable t; return t; }
}

inline void initFastMath() { (void)fastmath_detail::table(); }

/** sin(2π·turns) for any real `turns` (float math, truncation instead of floor()). */
inline float fastSinTurns(float turns) {
    using namespace fastmath_detail;
    float f = turns - float(int(turns));          // cvttss2si: no libm call
    if (f < 0.f) f += 1.f;
    const float x = f * float(kSineSize);
    int i = int(x);
    if (i >= kSineSize) i = kSineSize - 1;
    const float fr = x - float(i);
    const float* t = table().v;
    return t[i] + (t[i + 1] - t[i]) * fr;
}
/** sin(x), x in radians. */
inline float fastSin(float x) { return fastSinTurns(x * 0.15915494f); }
/** cos(x), x in radians. */
inline float fastCos(float x) { return fastSinTurns(x * 0.15915494f + 0.25f); }

} // namespace SonusDSP
