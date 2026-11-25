#pragma once
#include "../modules/StagePianoEngine.h"
#include "../modules/StageSynthModule.h"   // ← Necesario para Wavetable, SYNTH_NUM_LFOS, etc.
#include "ParameterRegistry.h"
#include "../control/ControlMappings.h"
#include <nlohmann/json.hpp>              // ← Necesario para json
#include <memory>
#include <string>
#include <vector>

namespace SonusDSP {

using json = nlohmann::json;              // ← Alias global al namespace

class WorkstationHost : public StagePianoEngine {
public:
    WorkstationHost();

    ParameterRegistry& getRegistry() { return registry_; }
    ControlMappings& getMappings() { return mappings_; }
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
    void applySynthPresetJsonImpl(const json& p);
    /** One synth layer from its preset JSON (missing fields take the defaults). */
    void applySynthLayerJson(size_t layer, const json& lp);
    void applyOrganPresetJson(const json& p);
    bool loadOrganSample(const std::string& path);
    void addWavetable(const Wavetable& wt);
    const std::vector<Wavetable>& getWavetableBank() const;
    std::shared_ptr<const std::vector<Wavetable>> getWavetableSnapshot() const;

private:
    void registerAllParameters();
    ParameterRegistry registry_;
    ControlMappings mappings_{registry_};   // after registry_: constructed from it
};

} // namespace SonusDSP