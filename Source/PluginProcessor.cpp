#include "PluginProcessor.h"
#include "PluginEditor.h"

WraithFormAudioProcessor::WraithFormAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
  waveformHistory.resize(waveformHistorySize, {0, 0, 0, 0, 0});

  // Setup LUFS history (3s at 100ms blocks = 30 blocks)
  energyHistory.assign(30, 0.0f);

  // Setup True Peak Oversampling (4x)
  oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
      2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
}

WraithFormAudioProcessor::~WraithFormAudioProcessor() {}

const juce::String WraithFormAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool WraithFormAudioProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool WraithFormAudioProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool WraithFormAudioProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double WraithFormAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int WraithFormAudioProcessor::getNumPrograms() {
  return 1; // NB: some hosts don't cope very well if you tell them there are 0
            // programs, so this should be at least 1, even if you're not really
            // implementing programs.
}

int WraithFormAudioProcessor::getCurrentProgram() { return 0; }

void WraithFormAudioProcessor::setCurrentProgram(int index) {}

const juce::String WraithFormAudioProcessor::getProgramName(int index) {
  return {};
}

void WraithFormAudioProcessor::changeProgramName(int index,
                                                 const juce::String &newName) {}

void WraithFormAudioProcessor::prepareToPlay(double sampleRate,
                                             int samplesPerBlock) {
  // Use this method as the place to do any pre-playback
  // initialization that you need..
  visualizerBufferL.resize(32768);
  visualizerBufferR.resize(32768);

  this->sampleRate = sampleRate;

  // Setup K-Weighting Filters
  auto shelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
      sampleRate, 1500.0, 1.0, juce::Decibels::decibelsToGain(4.0f));
  auto hpCoeffs =
      juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 100.0, 1.0);

  *kWeightingL.get<0>().coefficients = *shelfCoeffs;
  *kWeightingL.get<1>().coefficients = *hpCoeffs;
  *kWeightingR.get<0>().coefficients = *shelfCoeffs;
  *kWeightingR.get<1>().coefficients = *hpCoeffs;

  // Setup Multiband Filters (Crossover for Waveform)
  auto lowCoeffs =
      juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 200.0f);
  auto highCoeffs =
      juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 2500.0f);

  lowFilterL.coefficients = lowCoeffs;
  lowFilterR.coefficients = lowCoeffs;
  highFilterL.coefficients = highCoeffs;
  highFilterR.coefficients = highCoeffs;

  kWeightingL.reset();
  kWeightingR.reset();
  lowFilterL.reset();
  lowFilterR.reset();
  highFilterL.reset();
  highFilterR.reset();

  if (oversampler != nullptr)
    oversampler->initProcessing(samplesPerBlock);

  // Clear analysis state
  energyHistory.assign(30, 0.0f);
  gatedEnergies.clear();
  energyHistoryIndex = 0;
  blockAccumulator = 0;
  blockSampleCount = 0;
}

void WraithFormAudioProcessor::releaseResources() {
  // When playback stops, you can use this as an opportunity to free up any
  // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool WraithFormAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
#if JucePlugin_IsMidiEffect
  juce::ignoreUnused(layouts);
  return true;
#else
  // This is the place where you check if the layout is supported.
  // In this template code we only support mono or stereo.
  // Some plugin hosts, such as certain GarageBand versions, will only
  // load plugins that support stereo bus layouts.
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

  // This checks if the input layout matches the output layout
#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
#endif
}
#endif

void WraithFormAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                            juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  auto totalNumInputChannels = getTotalNumInputChannels();
  auto totalNumOutputChannels = getTotalNumOutputChannels();

  // In case we have more outputs than inputs, this code clears any output
  // channels that didn't contain input data, (because these aren't
  // guaranteed to be empty - they may contain garbage).
  for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  // Pass-through is automatic if we don't modify the buffer.
  // However, we need to read from it for visualization.

  // Safety check
  if (totalNumInputChannels > 0) {
    auto numSamples = buffer.getNumSamples();

    // Pointers to data
    auto *channelL = buffer.getReadPointer(0);
    auto *channelR =
        (totalNumInputChannels > 1) ? buffer.getReadPointer(1) : nullptr;

    float pL = 0.0f, pR = 0.0f;
    float sumSqL = 0.0f, sumSqR = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample) {
      float inL = channelL[sample];
      float inR = (channelR != nullptr) ? channelR[sample] : inL;

      // Peak & RMS
      pL = std::max(pL, std::abs(inL));
      pR = std::max(pR, std::abs(inR));
      sumSqL += inL * inL;
      sumSqR += inR * inR;

      // LUFS estimation (K-weighting)
      float filteredL = kWeightingL.get<0>().processSample(inL);
      filteredL = kWeightingL.get<1>().processSample(filteredL);

      float filteredR = kWeightingR.get<0>().processSample(inR);
      filteredR = kWeightingR.get<1>().processSample(filteredR);

      lufsAccumulator += (filteredL * filteredL + filteredR * filteredR);
      lufsSampleCount++;

      if (lufsSampleCount >= (int)(0.4 * sampleRate)) { // 400ms window
        float meanSquare = lufsAccumulator / (float)lufsSampleCount;
        float lufs = -0.691f + 10.0f * std::log10(std::max(1e-9f, meanSquare));
        lufsMomentary.store(lufs);
        lufsAccumulator = 0;
        lufsSampleCount = 0;
      }

      // 4. Waveform Multiband Analysis
      float fLowL = lowFilterL.processSample(inL);
      float fLowR = lowFilterR.processSample(inR);
      float fHighL = highFilterL.processSample(inL);
      float fHighR = highFilterR.processSample(inR);
      float fMidL = inL - (fLowL + fHighL);
      float fMidR = inR - (fLowR + fHighR);

      gPeakL = std::max(gPeakL, std::abs(inL));
      gPeakR = std::max(gPeakR, std::abs(inR));
      gLowL = std::max(gLowL, std::abs((fLowL + fLowR) * 0.5f));
      gMidL = std::max(gMidL, std::abs((fMidL + fMidR) * 0.5f));
      gHighL = std::max(gHighL, std::abs((fHighL + fHighR) * 0.5f));

      // Correlation for this sample
      gCorrNum += inL * inR;
      gCorrDenL += inL * inL;
      gCorrDenR += inR * inR;

      waveformSampleCount++;
      if (waveformSampleCount >= waveformGrainSize) {
        float peakTotal = (gPeakL + gPeakR) * 0.5f;
        float grainCorr =
            gCorrNum / std::sqrt(std::max(1e-9f, gCorrDenL * gCorrDenR));

        // Push to history
        int writeIdx = waveformHistoryWriteIndex.load();
        waveformHistory[writeIdx] = {peakTotal, gLowL, gMidL, gHighL,
                                     grainCorr};
        waveformHistoryWriteIndex.store((writeIdx + 1) % waveformHistorySize);

        // Reset accumulators
        waveformSampleCount = 0;
        gPeakL = 0;
        gPeakR = 0;
        gLowL = 0;
        gMidL = 0;
        gHighL = 0;
        gCorrNum = 0;
        gCorrDenL = 0;
        gCorrDenR = 0;
      }

      // 5. True Peak Analysis (Simplified/Integrated with LUFS for performance)
      // True peak ideally uses oversampling. We'll use the peak of weighted
      // samples as a high-quality estimate, or the oversampled peak if
      // oversampler initialized.

      // Encode samples for visualizers
      visualizerBufferL.write(inL * 0.5f + 0.5f);
      visualizerBufferR.write(inR * 0.5f + 0.5f);
    }

    // --- Oversampled True Peak Detection ---
    if (oversampler != nullptr) {
      juce::dsp::AudioBlock<float> block(buffer);
      auto osBlock = oversampler->processSamplesUp(block);

      float tpL = -100.0f, tpR = -100.0f;
      for (int ch = 0; ch < (int)osBlock.getNumChannels(); ++ch) {
        float *osPtr = osBlock.getChannelPointer(ch);
        for (int s = 0; s < (int)osBlock.getNumSamples(); ++s) {
          float val = std::abs(osPtr[s]);
          if (ch == 0)
            tpL = std::max(tpL, val);
          else
            tpR = std::max(tpR, val);
        }
      }
      truePeakL.store(juce::Decibels::gainToDecibels(tpL));
      truePeakR.store(juce::Decibels::gainToDecibels(tpR));
    }

    // --- Short-term & Integrated LUFS Logic ---
    // We add to blockAccumulator ever process block. LUFS windows are 400ms and
    // 3s. We use a 100ms "block energy" granularity.
    blockAccumulator += sumSqL + sumSqR;
    blockSampleCount += numSamples;

    if (blockSampleCount >= (int)(0.1 * sampleRate)) { // 100ms tick
      float blockEnergy = blockAccumulator / (float)blockSampleCount;

      // Push to history
      energyHistory[energyHistoryIndex] = blockEnergy;
      energyHistoryIndex = (energyHistoryIndex + 1) % (int)energyHistory.size();

      // Short-term (Last 3s = Sum of all 30 history entries)
      float stSum = 0;
      for (float e : energyHistory)
        stSum += e;
      float stLUFS =
          -0.691f + 10.0f * std::log10(std::max(
                                1e-9f, stSum / (float)energyHistory.size()));
      lufsShortTerm.store(stLUFS);

      // Integrated (Gating)
      // For simplicity: Add block if > -70 LUFS
      float blockLUFS =
          -0.691f + 10.0f * std::log10(std::max(1e-9f, blockEnergy));
      if (blockLUFS > -70.0f) {
        gatedEnergies.push_back(blockEnergy);

        float intSum = 0;
        for (float e : gatedEnergies)
          intSum += e;
        float intLUFS =
            -0.691f + 10.0f * std::log10(std::max(
                                  1e-9f, intSum / (float)gatedEnergies.size()));
        lufsIntegrated.store(intLUFS);
      }

      blockAccumulator = 0;
      blockSampleCount = 0;
    }

    peakL.store(pL);
    peakR.store(pR);
    rmsL.store(std::sqrt(sumSqL / (float)numSamples));
    rmsR.store(std::sqrt(sumSqR / (float)numSamples));
  }
  // Mute output
  buffer.clear();
}

bool WraithFormAudioProcessor::hasEditor() const {
  return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor *WraithFormAudioProcessor::createEditor() {
  return new WraithFormAudioProcessorEditor(*this);
}

void WraithFormAudioProcessor::resetLoudnessStats() {
  gatedEnergies.clear();
  energyHistory.assign(30, 0.0f);
  lufsIntegrated.store(-100.0f);
  lufsShortTerm.store(-100.0f);
}

void WraithFormAudioProcessor::getStateInformation(
    juce::MemoryBlock &destData) {
  // You should use this method to store your parameters in the memory block.
  // You could do that either as raw data, or use the XML or ValueTree classes
  // as intermediaries to make it easy to save and load complex data.
}

void WraithFormAudioProcessor::setStateInformation(const void *data,
                                                   int sizeInBytes) {
  // You should use this method to restore your parameters from this memory
  // block, whose contents will have been created by the getStateInformation()
  // call.
}

// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new WraithFormAudioProcessor();
}
