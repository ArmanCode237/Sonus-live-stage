#pragma once
#include "../modules/NordPianoEngine.h"
#include "../modules/NordSynthModule.h"   // ← Necesario para Wavetable, SYNTH_NUM_LFOS, etc.
#include "ParameterRegistry.h"
#include <nlohmann/json.hpp>              // ← Necesario para json
#include <string>
#include <vector>

namespace NordDSP {

using json = nlohmann::json;              // ← Alias global al namespace

class WorkstationHost : public NordPianoEngine {
public:
    WorkstationHost();

    ParameterRegistry& getRegistry() { return registry_; }
    bool applyParamById(const std::string& id, float val, bool normalized = false);

    // --- Helpers para Presets (Synth & Organ) ---
    bool getSynthEnabled() const;
    float getSynthMasterVol() const;
    float getSynthMacroCutoff() const;
    json getSynthLayersJson() const;
    json getSynthLFOsJson() const;
    json getSynthEnvelopesJson() const;
    json getOrganParamsJson() const;
    void applySynthPresetJson(const json& p);
    void applyOrganPresetJson(const json& p);
    bool loadOrganSample(const std::string& path);
    void addWavetable(const Wavetable& wt);
    const std::vector<Wavetable>& getWavetableBank() const;

private:
    void registerAllParameters();
    ParameterRegistry registry_;
};

} // namespace NordDSP