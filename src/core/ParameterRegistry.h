#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <array>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace NordDSP {

struct MidiMappingBinding {
    std::string paramId;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    bool active = false;
};

class ParameterRegistry {
public:
    using SetterFn = std::function<void(float)>;

    void registerParam(const std::string& id, float minV, float maxV, SetterFn setter) {
        std::lock_guard<std::mutex> lock(mutex_);
        params_[id] = {minV, maxV, std::move(setter)};
    }

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
            return true;
        }
        return false;
    }

    void armMidiLearn(const std::string& paramId) {
        std::lock_guard<std::mutex> lock(mutex_);
        learningParamId_ = paramId;
        midiLearnActive_.store(true);
    }

    void cancelMidiLearn() {
        midiLearnActive_.store(false);
    }

    bool handleMidiCC(int ccNumber, int ccValue127) {
        if (ccNumber < 0 || ccNumber >= 128) return false;

        if (midiLearnActive_.exchange(false)) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = params_.find(learningParamId_);
            if (it != params_.end()) {
                ccBindings_[ccNumber] = {learningParamId_, it->second.minVal, it->second.maxVal, true};
                lastLearnedCC_.store(ccNumber);
            }
        }

        MidiMappingBinding binding;
        SetterFn fn;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            binding = ccBindings_[ccNumber];
            if (!binding.active) return false;
            auto it = params_.find(binding.paramId);
            if (it == params_.end()) return false;
            fn = it->second.setter;
        }

        if (fn) {
            float norm = std::clamp(static_cast<float>(ccValue127) / 127.0f, 0.0f, 1.0f);
            float scaled = binding.minVal + norm * (binding.maxVal - binding.minVal);
            fn(scaled);
            return true;
        }
        return false;
    }

    nlohmann::json exportBindingsJson() {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json arr = nlohmann::json::array();
        for (int cc = 0; cc < 128; ++cc) {
            if (ccBindings_[cc].active) {
                arr.push_back({
                    {"cc", cc},
                    {"paramId", ccBindings_[cc].paramId},
                    {"min", ccBindings_[cc].minVal},
                    {"max", ccBindings_[cc].maxVal}
                });
            }
        }
        return arr;
    }

    void importBindingsJson(const nlohmann::json& arr) {
        if (!arr.is_array()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : arr) {
            int cc = item.value("cc", -1);
            std::string pid = item.value("paramId", "");
            if (cc >= 0 && cc < 128 && !pid.empty() && params_.count(pid)) {
                ccBindings_[cc] = {pid, params_[pid].minVal, params_[pid].maxVal, true};
            }
        }
    }

    int getLastLearnedCC() const { return lastLearnedCC_.load(); }
    bool isLearning() const { return midiLearnActive_.load(); }

private:
    struct ParamEntry {
        float minVal = 0.0f;
        float maxVal = 1.0f;
        SetterFn setter;
    };

    std::mutex mutex_;
    std::unordered_map<std::string, ParamEntry> params_;
    std::array<MidiMappingBinding, 128> ccBindings_{};
    std::string learningParamId_;
    std::atomic<bool> midiLearnActive_{false};
    std::atomic<int> lastLearnedCC_{-1};
};

} // namespace NordDSP