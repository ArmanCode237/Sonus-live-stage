#pragma once
// Read-only, RAII-backed source file for CNSP decoding. Mapping avoids an
// extra compressed-bank allocation/copy; PCM storage remains owned and stable.
// On platforms where mapping fails, fall back to the original buffered read.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace NordDSP {
class ReadOnlyFileBytes {
public:
    explicit ReadOnlyFileBytes(const std::string& filename) {
#ifdef _WIN32
        HANDLE file = CreateFileW(std::filesystem::path(filename).c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER length{};
            if (GetFileSizeEx(file, &length) && length.QuadPart > 0 &&
                    static_cast<unsigned long long>(length.QuadPart) <=
                    std::numeric_limits<size_t>::max()) {
                HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
                if (mapping) {
                    auto view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
                    if (view) { data_ = view; size_ = size_t(length.QuadPart); mapped_ = true; }
                    CloseHandle(mapping);
                }
            }
            CloseHandle(file);
        }
#else
        const int fd = ::open(filename.c_str(), O_RDONLY);
        if (fd >= 0) {
            struct stat st{};
            if (::fstat(fd, &st) == 0 && st.st_size > 0 &&
                    static_cast<unsigned long long>(st.st_size) <=
                    std::numeric_limits<size_t>::max()) {
                void* view = ::mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
                if (view != MAP_FAILED) { data_ = static_cast<const uint8_t*>(view); size_ = size_t(st.st_size); mapped_ = true; }
            }
            ::close(fd);
        }
#endif
        if (mapped_) return;
        std::ifstream in(filename, std::ios::binary | std::ios::ate);
        if (!in) return;
        const auto len = in.tellg();
        if (len <= 0 || static_cast<uint64_t>(len) > std::numeric_limits<size_t>::max()) return;
        try { fallback_.resize(static_cast<size_t>(len)); }
        catch (...) { return; }
        in.seekg(0);
        if (!in.read(reinterpret_cast<char*>(fallback_.data()), len)) { fallback_.clear(); return; }
        data_ = fallback_.data(); size_ = fallback_.size();
    }
    ~ReadOnlyFileBytes() {
        if (mapped_) {
#ifdef _WIN32
            UnmapViewOfFile(data_);
#else
            ::munmap(const_cast<uint8_t*>(data_), size_);
#endif
        }
    }
    ReadOnlyFileBytes(const ReadOnlyFileBytes&) = delete;
    ReadOnlyFileBytes& operator=(const ReadOnlyFileBytes&) = delete;
    const uint8_t& operator[](size_t offset) const noexcept { return data_[offset]; }
    const uint8_t* data() const noexcept { return data_; }
    size_t size() const noexcept { return size_; }
    bool good() const noexcept { return data_ && size_; }
    bool mapped() const noexcept { return mapped_; }
private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    bool mapped_ = false;
    std::vector<uint8_t> fallback_;
};
} // namespace NordDSP
