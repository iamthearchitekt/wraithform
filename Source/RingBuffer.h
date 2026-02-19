#pragma once
#include <atomic>
#include <vector>

// Simple Single-Producer Single-Consumer Lock-Free Ring Buffer
// Tailored for audio visualization (one writer, one reader)
template <typename T> class RingBuffer {
public:
  RingBuffer() { resize(8192); }

  void resize(size_t size) {
    buffer.resize(size);
    writeIndex.store(0);
    readIndex.store(0);
    bufferSize = size;
    mask = size - 1;
    // Ensure size is power of 2 for fast masking, or use modulo
  }

  void write(T value) {
    size_t currentWrite = writeIndex.load(std::memory_order_relaxed);
    buffer[currentWrite] = value;
    writeIndex.store((currentWrite + 1) % bufferSize,
                     std::memory_order_release);
  }

  // Read latest N samples into a destination, handling wrapping
  // This is a "snapshot" read, doesn't need to advance read index strictly for
  // visualization But for correct time-domain we want to read what's new or
  // just the last N samples. For this oscilloscope, we want a window of
  // history.
  void readHistory(std::vector<T> &dest, size_t numSamples) {
    size_t currentWrite = writeIndex.load(std::memory_order_acquire);

    // We want the *latest* numSamples ending at currentWrite
    // Since it's a ring buffer, we might need to wrap around.

    if (dest.size() != numSamples)
      dest.resize(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
      // Calculate index: currentWrite - numSamples + i
      // But handle negative wrapping carefully
      size_t idx = (currentWrite + bufferSize - numSamples + i) % bufferSize;
      dest[i] = buffer[idx];
    }
  }

  size_t getWriteIndex() const {
    return writeIndex.load(std::memory_order_acquire);
  }

  T readSample(size_t index) const { return buffer[index % bufferSize]; }

private:
  std::vector<T> buffer;
  std::atomic<size_t> writeIndex{0};
  std::atomic<size_t> readIndex{0};
  size_t bufferSize{0};
  size_t mask{0};
};
