#pragma once
// Catalogue of every writable engine parameter: ID → range and setter.
// MIDI / OSC mappings live in control/ControlMappings (scopes, learn, pickup).
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <vector>
#include <nlohmann/json.hpp>

namespace SonusDSP {

class ParameterRegistry {
public:
    using SetterFn = std::function<void(float)>;
    using WriteObserver = std::function<void(const std::string&, float)>;

    void registerParam(const std::string& id, float minV, float maxV, SetterFn setter) {
        std::lock_guard<std::mutex> lock(mutex_);
        params_[id] = {minV, maxV, std::move(setter)};
    }

    /** Set once at start-up (before any thread writes). Called after every successful write. */
    void setWriteObserver(WriteObserver observer) { observer_ = std::move(observer); }

    bool setParamValue(const std::string& id, float value, bool isNormalized0to1 = false) {
        SetterFn fn;
        float finalVal = value;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = params_.find(id);
            if (it == params_.end()) return false;
            if (isNormalized0to1) {
                finalVal = it->second.minVal + std::clamp(value, 0.0f, 1.0f) * (it->second.maxVal - it->second.minVal);
            }
            fn = it->second.setter;
        }
        if (fn) {
            fn(finalVal);
            valueRevision_.fetch_add(1,std::memory_order_relaxed);
            if (observer_) observer_(id, finalVal);
            return true;
        }
        return false;
    }

    nlohmann::json exportParameterCatalog() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> ids;
        ids.reserve(params_.size());
        for (const auto& item : params_) ids.push_back(item.first);
        std::sort(ids.begin(),ids.end());
        nlohmann::json catalog=nlohmann::json::array();
        for(const auto& id:ids){
            const auto& parameter=params_.at(id);
            catalog.push_back({{"id",id},{"min",parameter.minVal},{"max",parameter.maxVal}});
        }
        return catalog;
    }

    bool hasParam(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mutex_); return params_.count(id) != 0;
    }
    bool paramRange(const std::string& id, float& minV, float& maxV) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = params_.find(id);
        if (it == params_.end()) return false;
        minV = it->second.minVal; maxV = it->second.maxVal;
        return true;
    }

    uint64_t valueRevision() const {return valueRevision_.load(std::memory_order_relaxed);}

private:
    struct ParamEntry {
        float minVal = 0.0f;
        float maxVal = 1.0f;
        SetterFn setter;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ParamEntry> params_;
    WriteObserver observer_;
    std::atomic<uint64_t> valueRevision_{0};
};

} // namespace SonusDSP
