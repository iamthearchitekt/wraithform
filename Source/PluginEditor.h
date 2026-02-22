#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>
#include <memory>
#include <vector>

//==============================================================================
class WraithFormAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       public juce::OpenGLRenderer {
public:
  WraithFormAudioProcessorEditor(WraithFormAudioProcessor &);
  ~WraithFormAudioProcessorEditor() override;

  //==============================================================================
  void paint(juce::Graphics &) override;
  void resized() override;

  // OpenGLRenderer overrides
  void newOpenGLContextCreated() override;
  void renderOpenGL() override;
  void openGLContextClosing() override;
  // Interaction
  void mouseDown(const juce::MouseEvent &event) override;
  void toggleDetachedWindows();

private:
  WraithFormAudioProcessor &audioProcessor;
  juce::OpenGLContext openGLContext;

  // Shaders
  std::unique_ptr<juce::OpenGLShaderProgram> oscilloscopeShader;
  std::unique_ptr<juce::OpenGLShaderProgram> spectrogramShader;
  std::unique_ptr<juce::OpenGLShaderProgram> circularShader;
  std::unique_ptr<juce::OpenGLShaderProgram> seratoShader;
  std::unique_ptr<juce::OpenGLShaderProgram> splashShader;
  std::unique_ptr<juce::OpenGLShaderProgram> fireballShader;
  std::unique_ptr<juce::OpenGLShaderProgram> phaseShader;

  void createShaders();
  void renderSpectrogram();
  void renderOscilloscope();
  void renderCircularOscilloscope();
  void renderSeratoWaveform();
  void renderPhaseMeter();
  void renderMeters();
  void renderLoudnessDashboard();
  void renderQuadView();
  void updateAudioData();

  // Mode
  enum class VisualizerMode {
    Loading,
    Oscilloscope,
    Circular,
    Serato,
    Spectrogram,
    PhaseMeter,
    QuadView
  };
  enum class FFTSize { Transient = 9, AllRound = 11, Harmonic = 13 };
  VisualizerMode currentMode = VisualizerMode::Loading;

  bool isFullScreen = false;
  juce::Rectangle<int> quadBtnRect, multiBtnRect, resetBtnRect,
      fullScreenBtnRect;

  // Waveform / Serato History (Mirroring Processor)
  std::vector<WaveformPoint> waveformHistory;
  unsigned int waveformHistoryTexture = 0;
  static const int historySize = 1024;

  // Correlation Meter
  float currentCorrelation = 0.0f;
  float smoothedCorrelation = 0.0f;

  // Loading Screen & Transition
  juce::Image splashImage;
  unsigned int splashTextureID = 0;
  float loadingAlpha = 1.0f;
  bool isTransitioning = true;

  // HD Spectrogram Controls
  FFTSize currentFFTSize = FFTSize::AllRound;
  void updateFFTSize(FFTSize newSize);
  bool isFrozen = false;
  float zoomLevel = 1.0f, scrollOffset = 0.0f;
  float minDb = -90.0f, maxDb = 0.0f;

  // Metering Sidebar
  bool isSideBarVisible = true;
  float sideBarAnimation = 1.0f; // 0..1
  float peakHoldL = 0, peakHoldR = 0, peakHoldLUFS = -100.0f;

  // Oscilloscope Data
  unsigned int audioTextureID = 0;
  std::vector<float> textureData; // Mono for Oscilloscope
  std::vector<juce::Point<float>>
      phaseData; // Stereo (x=L, y=R) for Phase Meter
  static const int textureSize = 4096;
  int findTriggerPoint(const std::vector<float> &data, int size);

  // FFT Data
  std::unique_ptr<juce::dsp::FFT> forwardFFT;
  std::unique_ptr<juce::dsp::WindowingFunction<float>> windowingFunction;
  std::vector<float> fftBuffer;
  std::vector<float> fftOutput;
  static const int fftOrder = 11; // 2048 points
  static const int fftSize = 1 << fftOrder;

  // CPU-Backed Spectrogram Image (Reliable)
  juce::Image spectrogramImage;
  unsigned int spectrogramTextureID = 0;

  // Geometry
  std::vector<float> quadVertices;

  // Dashboard button hitboxes

  std::atomic<int> numDetachedWindows{0};

  // Detached Window System (Class & Vector)
  class DetachedWindow : public juce::DocumentWindow {
  public:
    DetachedWindow(const juce::String &name,
                   WraithFormAudioProcessorEditor &owner, VisualizerMode mode);
    ~DetachedWindow() override;
    void closeButtonPressed() override;

  private:
    WraithFormAudioProcessorEditor &owner;
    VisualizerMode mode;

    struct ExternalRenderer : public juce::OpenGLRenderer {
      ExternalRenderer(DetachedWindow &w) : window(w) {}
      void newOpenGLContextCreated() override {}
      void openGLContextClosing() override {}
      void renderOpenGL() override;
      DetachedWindow &window;
    };

    ExternalRenderer renderer;
    juce::OpenGLContext openGLContext;

    // Allow ExternalRenderer to access private members
    friend struct ExternalRenderer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DetachedWindow)
  };

  std::vector<std::unique_ptr<DetachedWindow>> detachedWindows;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WraithFormAudioProcessorEditor)
};
