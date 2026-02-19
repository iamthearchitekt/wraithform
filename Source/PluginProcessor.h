#pragma once

#include "RingBuffer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

struct WaveformPoint {
  float amp;
  float low;
  float mid;
  float high;
  float correlation;
};

class WraithFormAudioProcessor : public juce::AudioProcessor {
public:
  WraithFormAudioProcessor();
  ~WraithFormAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
#endif

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  void resetLoudnessStats();

  // Public for Editor access
  RingBuffer<float> visualizerBufferL;
  RingBuffer<float> visualizerBufferR;

  // Waveform History for Serato Mode
  static const int waveformHistorySize = 1024;
  std::vector<WaveformPoint> waveformHistory;
  std::atomic<int> waveformHistoryWriteIndex{0};

  // Metering Data
  std::atomic<float> peakL{0.0f}, peakR{0.0f};
  std::atomic<float> rmsL{0.0f}, rmsR{0.0f};
  std::atomic<float> lufsMomentary{-100.0f};
  std::atomic<float> lufsShortTerm{-100.0f};
  std::atomic<float> lufsIntegrated{-100.0f};
  std::atomic<float> truePeakL{-100.0f}, truePeakR{-100.0f};

private:
  // K-Weighting for LUFS
  using Filter = juce::dsp::IIR::Filter<float>;
  using FilterChain = juce::dsp::ProcessorChain<Filter, Filter>;
  FilterChain kWeightingL, kWeightingR;

  // 3-Band Filters for Waveform
  Filter lowFilterL, lowFilterR;
  Filter highFilterL, highFilterR;

  // Measurement State
  double sampleRate = 44100.0;
  float lufsAccumulator = 0.0f;
  int lufsSampleCount = 0;

  // Integrated / Short-term Helpers
  std::vector<float> energyHistory; // 100ms blocks
  std::vector<float> gatedEnergies; // For Integrated
  int energyHistoryIndex = 0;
  float blockAccumulator = 0;
  int blockSampleCount = 0;

  // True Peak Oversampling
  std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

  // Waveform Grain Accumulators
  int waveformSampleCount = 0;
  static const int waveformGrainSize = 256;
  float gPeakL = 0, gPeakR = 0;
  float gLowL = 0, gLowR = 0;
  float gMidL = 0, gMidR = 0;
  float gHighL = 0, gHighR = 0;
  float gCorrNum = 0, gCorrDenL = 0, gCorrDenR = 0;
};
