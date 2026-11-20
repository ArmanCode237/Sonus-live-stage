#pragma once
// v32.4 live log: everything written to std::cout / std::cerr is also appended, with a
// time stamp per line, to <user data>/logs/live-stage-YYYYMMDD.log (the last 14 files
// are kept). After a show, the log tells what happened: devices, reconnections,
// audio dropouts, load errors. Never used from the audio callback.
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>
#include <vector>
#include <algorithm>

namespace live_log {

class TeeBuf : public std::streambuf {
public:
    TeeBuf(std::streambuf* console, std::ofstream& file, std::mutex& mutex) : console_(console), file_(file), mutex_(mutex) {}
protected:
    int overflow(int ch) override {
        if (ch == traits_type::eof()) return traits_type::not_eof(ch);
        std::lock_guard<std::mutex> lock(mutex_);
        put(static_cast<char>(ch));
        return ch;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::streamsize i = 0; i < n; ++i) put(s[i]);
        return n;
    }
    int sync() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.flush();
        return console_ ? console_->pubsync() : 0;
    }
private:
    void put(char c) {
        if (console_) console_->sputc(c);
        if (!file_.is_open()) return;
        if (lineStart_) { file_ << stamp(); lineStart_ = false; }
        file_.put(c);
        if (c == '\n') { lineStart_ = true; file_.flush(); }
    }
    static std::string stamp() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d  ", tm.tm_hour, tm.tm_min, tm.tm_sec, int(ms));
        return buf;
    }
    std::streambuf* console_;
    std::ofstream& file_;
    std::mutex& mutex_;
    bool lineStart_ = true;
};

/** Mirrors std::cout and std::cerr into a dated log file under `dir`. Call once at startup. */
inline void install(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    static std::ofstream file;
    static std::mutex mutex;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char name[64];
    std::snprintf(name, sizeof name, "live-stage-%04d%02d%02d.log", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    file.open(dir / name, std::ios::app);
    if (file.is_open()) file << "\n==================== session " << tm.tm_hour << ":" << tm.tm_min << " ====================\n";
    // Keep the 14 most recent logs.
    std::vector<fs::path> logs;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().filename().string().rfind("live-stage-", 0) == 0) logs.push_back(e.path());
    std::sort(logs.begin(), logs.end());
    for (size_t i = 0; i + 14 < logs.size(); ++i) fs::remove(logs[i], ec);
    static TeeBuf out(std::cout.rdbuf(), file, mutex);
    static TeeBuf err(std::cerr.rdbuf(), file, mutex);
    std::cout.rdbuf(&out);
    std::cerr.rdbuf(&err);
}

} // namespace live_log
