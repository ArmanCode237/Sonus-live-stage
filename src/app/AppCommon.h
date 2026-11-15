#pragma once
// Shared aliases for the standalone application layer (former main.cpp).
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>

namespace NordDSP {}
using namespace NordDSP;
using json = nlohmann::json;
namespace fs = std::filesystem;
