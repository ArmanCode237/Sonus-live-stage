// Assertions carry the checks (some with side effects): keep them in Release builds.
#undef NDEBUG
// Multithreaded MIDI producer regression: HTTP and RtMidi may both enqueue.
#include "../src/modules/NordPianoEngine.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

int main() {
  NordDSP::NordPianoEngine engine;
  engine.setSampleRate(48000);
  constexpr int blocks = 180;
  constexpr int producers = 3;
  std::atomic<bool> start{false};
  std::vector<std::thread> workers;
  for (int t = 0; t < producers; ++t) {
    workers.emplace_back([&, t]() {
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      for (int j = 0; j < 2200; ++j) {
        int key = 48 + (t * 24 + j % 12) % 48;
        engine.noteOn(key, 100);
        engine.noteOff(key);
        if ((j & 63) == 0) engine.setSustainPedal(false);
      }
    });
  }
  start.store(true, std::memory_order_release);
  std::vector<float> buf(256 * 2);
  for (int j = 0; j < blocks; ++j) {
    engine.render(buf.data(), 256);
    for (auto f: buf) assert(std::isfinite(f));
  }
  for (auto& w: workers) w.join();
  engine.allNotesOff();
  for (int j = 0; j < 10; ++j) engine.render(buf.data(),256);
  std::cout << "PASS MPSC multi-source MIDI; overflow=" << engine.getMidiDroppedEvents() << "\n";
}
