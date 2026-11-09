#pragma once
#include "../modules/NordPianoEngine.h"
#include "ParameterRegistry.h"

namespace NordDSP {

class WorkstationHost : public NordPianoEngine {
public:
    WorkstationHost();

    ParameterRegistry& getRegistry() { return registry_; }
    bool applyParamById(const std::string& id, float val, bool normalized = false);

private:
    void registerAllParameters();
    ParameterRegistry registry_;
};

} // namespace NordDSP