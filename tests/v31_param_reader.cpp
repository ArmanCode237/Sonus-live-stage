// The C++ parameter reader (used by MIDI pickup, relative encoders and toggles) must
// resolve every registered parameter exactly like the UI's paramMap.ts.
// Fixtures: webui/src/params/__fixtures__ (resolved.json is written by vitest).
#include "control/ParamValueReader.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
using json = nlohmann::json;

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "webui/src/params/__fixtures__";
    auto load = [&](const char* name) { std::ifstream f(dir + "/" + name); return json::parse(f); };
    const json params = load("params.json"), resolved = load("resolved.json");
    int checked = 0, failures = 0;
    for (const auto& [id, expected] : resolved.items()) {
        const auto got = SonusDSP::readParamValue(params, id);
        const bool same = expected.is_null() ? !got : (got && std::abs(*got - expected.get<float>()) <= 1e-4f * std::max(1.f, std::abs(expected.get<float>())));
        if (!same) {
            ++failures;
            std::printf("MISMATCH %s: ts=%s cpp=%s\n", id.c_str(), expected.dump().c_str(), got ? std::to_string(*got).c_str() : "null");
        }
        ++checked;
    }
    std::printf("%d parameters, %d mismatches\n", checked, failures);
    if (failures || checked < 500) return 1;
    std::puts("PASS v31 parameter reader matches paramMap.ts");
}
