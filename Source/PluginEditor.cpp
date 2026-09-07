#include "PluginEditor.h"
#include "CircularOscilloscopeShader.h"
#include "CloudVortexShader.h"
#include "CosmicFlareShader.h"
#include "LiquidFireShader.h"
#include "PhaseCorrelationShader.h"
#include "PluginProcessor.h"
#include "SpectrogramShader.h"
#include "SplashCanvas.h"
#include "VectorscopeShader.h"
#include "VisualizerShader.h"
#include "VolumetricExplosionShader.h"
#include "WaveformShader.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_opengl/juce_opengl.h>

//==============================================================================


void WraithFormAudioProcessorEditor::updateFFTSize(FFTSize newSize) {
  currentFFTSize = newSize;
  pendingFFTOrder.store((int)newSize, std::memory_order_release);
}

void WraithFormAudioProcessorEditor::applyPendingFFTSize() {
  int order = pendingFFTOrder.exchange(0, std::memory_order_acq_rel);
  if (order <= 0) return;

  int size = 1 << order;
  forwardFFT = std::make_unique<juce::dsp::FFT>(order);
  windowingFunction = std::make_unique<juce::dsp::WindowingFunction<float>>(
      size, juce::dsp::WindowingFunction<float>::blackmanHarris);

  fftBuffer.assign(size * 2, 0.0f);
  fftOutput.assign(size / 2, 0.0f);
  fftInLBuffer.resize(size, 0.0f);
  fftInRBuffer.resize(size, 0.0f);
}

WraithFormAudioProcessorEditor::DetachedWindow::DetachedWindow(
    const juce::String &name, WraithFormAudioProcessorEditor &owner,
    VisualizerMode mode)
    : DocumentWindow(name, juce::Colours::black, DocumentWindow::allButtons),
      owner(owner), mode(mode), renderer(*this) {
  setResizable(true, true);
  setUsingNativeTitleBar(true);

  openGLContext.setRenderer(&renderer);
  openGLContext.attachTo(*this);
  openGLContext.setContinuousRepainting(true);

  setSize(400, 300);
  setVisible(true);
}

WraithFormAudioProcessorEditor::DetachedWindow::~DetachedWindow() {
  openGLContext.detach();
}

void WraithFormAudioProcessorEditor::DetachedWindow::closeButtonPressed() {
  owner.numDetachedWindows--;
  owner.toggleDetachedWindows();
}

void WraithFormAudioProcessorEditor::DetachedWindow::ExternalRenderer::
    renderOpenGL() {
  using namespace juce::gl;
  float scale = (float)window.openGLContext.getRenderingScale();

  // Ensure viewport matches window dimensions
  glViewport(0, 0, (GLint)juce::roundToInt(window.getWidth() * scale),
             (GLint)juce::roundToInt(window.getHeight() * scale));
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  std::unique_ptr<juce::LowLevelGraphicsContext> gl(
      juce::createOpenGLGraphicsContext(window.openGLContext, window.getWidth(),
                                        window.getHeight()));
  if (gl) {
    juce::Graphics g(*gl);
    g.addTransform(juce::AffineTransform::scale(scale));

    // Draw centered text / content
    g.setColour(juce::Colour(0xFFD5FFFF));
    g.setFont(20.0f);
    g.drawText(window.getName(), 0, 20, window.getWidth(), 30,
               juce::Justification::centredTop);

    // Simple 2D Visualization Fallback (Thread-Safe)
    g.setColour(juce::Colour(0xFFD5FFFF).withAlpha(0.8f));

    if (window.mode == VisualizerMode::Oscilloscope ||
        window.mode == VisualizerMode::Serato) {
      juce::Path p;
      auto &buffer = window.owner.textureData; // Access owner's data
      int h = window.getHeight();
      int w = window.getWidth();
      p.startNewSubPath(0, h / 2);
      for (int i = 0; i < w; i += 4) {
        // Map i to buffer index
        int idx = (i * buffer.size()) / w;
        if (idx < buffer.size()) {
          float s = buffer[idx];
          p.lineTo((float)i, h / 2 - s * (h * 0.4f));
        }
      }
      g.strokePath(p, juce::PathStrokeType(2.0f));
    } else if (window.mode == VisualizerMode::Circular) {
      g.drawEllipse(window.getWidth() / 2 - 100, window.getHeight() / 2 - 100,
                    200, 200, 2.0f);
      // Pulse effect
      auto &buffer = window.owner.textureData;
      if (!buffer.empty()) {
        float energy = std::abs(buffer[0]) * 50.0f;
        g.drawEllipse(window.getWidth() / 2 - (100 + energy),
                      window.getHeight() / 2 - (100 + energy),
                      (100 + energy) * 2, (100 + energy) * 2, 1.0f);
      }
    } else if (window.mode == VisualizerMode::Spectrogram) {
      // Draw some bars
      auto &buffer = window.owner.fftOutput;
      int w = window.getWidth();
      int h = window.getHeight();
      int barWidth = 4;
      for (int i = 0; i < w / barWidth; ++i) {
        int idx = (i * buffer.size()) / (w / barWidth);
        if (idx < buffer.size()) {
          float val = buffer[idx] * h;
          g.fillRect(i * barWidth, h - (int)val, barWidth - 1, (int)val);
        }
      }
    }

    // "NO SIGNAL" / Waiting text if buffers empty
    if (window.owner.textureData.empty() && window.owner.fftOutput.empty()) {
      g.setColour(juce::Colours::red);
      g.drawText("NO SIGNAL", 0, 0, window.getWidth(), window.getHeight(),
                 juce::Justification::centred);
    }
  }
}
WraithFormAudioProcessorEditor::WraithFormAudioProcessorEditor(
    WraithFormAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  forwardFFT = std::make_unique<juce::dsp::FFT>((int)FFTSize::AllRound);
  windowingFunction = std::make_unique<juce::dsp::WindowingFunction<float>>(
      (1 << (int)FFTSize::AllRound),
      juce::dsp::WindowingFunction<float>::WindowingMethod::blackmanHarris);

  setResizable(true, true);
  setSize(1280, 720);

  // Setup OpenGL
  openGLContext.setRenderer(this);
  openGLContext.attachTo(*this);
  openGLContext.setContinuousRepainting(true);

  // Audio Buffers setup
  int initialSize = 1 << (int)FFTSize::AllRound;
  textureData.resize(textureSize, 0.0f);
  phaseData.resize(textureSize, {0.0f, 0.0f}); // Init phase data
  fftBuffer.resize(initialSize * 2, 0.0f);
  fftOutput.resize(initialSize / 2, 0.0f);

  // Pre-allocate scratch buffers to eliminate heap allocations in render loop
  const int searchSize = textureSize * 2;
  wideLBuffer.resize(searchSize, 0.0f);
  wideRBuffer.resize(searchSize, 0.0f);
  wideSumBuffer.resize(searchSize, 0.0f);
  phaseLBuffer.resize(1024, 0.0f);
  phaseRBuffer.resize(1024, 0.0f);
  fftInLBuffer.resize(1 << (int)FFTSize::Harmonic, 0.0f);
  fftInRBuffer.resize(1 << (int)FFTSize::Harmonic, 0.0f);
  hardVectorsBuffer.reserve(1024);
  vectorScopeData.resize(1024, {0.0f, 0.0f});

  waveformHistory.resize(historySize, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f});

  // CPU-Backed Spectrogram Init (Safe Mode)
  // 512px wide (time), 512px tall (freq) - efficient size
  spectrogramImage = juce::Image(juce::Image::RGB, 512, 512, true);
  spectrogramImage.clear(spectrogramImage.getBounds(), juce::Colours::black);

  currentMode = VisualizerMode::Oscilloscope; // Boot directly to Oscilloscope
  isTransitioning = false;                    // No splash transition
  loadingAlpha = 0.0f;
  // splashImage = juce::Image(); // Force invalid to prevent ANY attempt to
  // load it

  // Setup Geometry
  quadVertices = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
                  1.0f,  -1.0f, 1.0f, 1.0f,  -1.0f, 1.0f};

  // Initial filter setup is now in side the processor.

  // Load loading screen image (Base64 Embedding)
  // detailed failure fallbacks removed in favor of direct memory load
  if (!splashImage.isValid()) {
    try {
      juce::MemoryOutputStream mos;
      if (juce::Base64::convertFromBase64(mos,
                                          SplashCanvas::loadingScreenBase64)) {
        juce::MemoryInputStream mis(mos.getMemoryBlock(), false);
        splashImage = juce::ImageFileFormat::loadFrom(mis);
      }
    } catch (...) {
    }
  }

  // Fallback: If Base64 fails, we just have no splash.
}

WraithFormAudioProcessorEditor::~WraithFormAudioProcessorEditor() {}

//==============================================================================
//==============================================================================
//==============================================================================
void WraithFormAudioProcessorEditor::paint(juce::Graphics &g) {
  // Allow OpenGL to clear the screen
}

void WraithFormAudioProcessorEditor::resized() {
  // Add layout logic here if needed
}

void WraithFormAudioProcessorEditor::mouseDown(const juce::MouseEvent &e) {
  // Sidebar toggle on right edge (40px)
  if (e.x > getWidth() - 40) {
    isSideBarVisible = !isSideBarVisible;
    openGLContext.triggerRepaint();
    return;
  }

  if (e.mods.isRightButtonDown()) {
    // Cycle FFT sizes in Spectrogram mode
    if (currentMode == VisualizerMode::Spectrogram) {
      if (currentFFTSize == FFTSize::Transient)
        updateFFTSize(FFTSize::AllRound);
      else if (currentFFTSize == FFTSize::AllRound)
        updateFFTSize(FFTSize::Harmonic);
      else
        updateFFTSize(FFTSize::Transient);
    } else if (currentMode == VisualizerMode::Vectorscope) {
      isLissajousXY = !isLissajousXY;
      openGLContext.triggerRepaint();
    }
  } else if (e.mods.isMiddleButtonDown()) {
    isFrozen = !isFrozen;
  } else {
    // RESET Button Hitbox (Bottom Left)
    if (e.x > 20 && e.x < 75 && e.y > getHeight() - 40) {
      audioProcessor.resetLoudnessStats();
      return;
    }

    // QUAD View Toggle (Beside Reset)
    if (e.x > 80 && e.x < 145 && e.y > getHeight() - 40) {
      currentMode = (currentMode == VisualizerMode::QuadView)
                        ? VisualizerMode::Oscilloscope
                        : VisualizerMode::QuadView;
      openGLContext.triggerRepaint();
      return;
    }

    // COLOR MODE Toggle (Beside QUAD)
    if (e.x > 150 && e.x < 225 && e.y > getHeight() - 40) {
      if (currentColorMode == ColorMode::Wraith)
        currentColorMode = ColorMode::UV;
      else if (currentColorMode == ColorMode::UV)
        currentColorMode = ColorMode::Infrared;
      else if (currentColorMode == ColorMode::Infrared)
        currentColorMode = ColorMode::Heat;
      else if (currentColorMode == ColorMode::Heat)
        currentColorMode = ColorMode::Plasma;
      else
        currentColorMode = ColorMode::Wraith;
      openGLContext.triggerRepaint();
      return;
    }

    // Left click cycles modes
    if (currentMode == VisualizerMode::Oscilloscope)
      currentMode = VisualizerMode::Spectrogram;
    else if (currentMode == VisualizerMode::Spectrogram)
      currentMode = VisualizerMode::Serato;
    else if (currentMode == VisualizerMode::Serato)
      currentMode = VisualizerMode::Circular;
    else if (currentMode == VisualizerMode::Circular)
      currentMode = VisualizerMode::PhaseMeter;
    else if (currentMode == VisualizerMode::PhaseMeter)
      currentMode = VisualizerMode::Vectorscope;
    else if (currentMode == VisualizerMode::Vectorscope)
      currentMode = VisualizerMode::CosmicFlare;
    else if (currentMode == VisualizerMode::CosmicFlare)
      currentMode = VisualizerMode::VolumetricExplosion;
    else if (currentMode == VisualizerMode::VolumetricExplosion)
      currentMode = VisualizerMode::QuadView;
    else if (currentMode == VisualizerMode::QuadView)
      currentMode = VisualizerMode::Oscilloscope;

    openGLContext.triggerRepaint();
  }
}

void WraithFormAudioProcessorEditor::toggleDetachedWindows() {
  if (numDetachedWindows.load() > 0) {
    detachedWindows.clear();
    numDetachedWindows = 0;
  }
}

int WraithFormAudioProcessorEditor::findTriggerPoint(
    const std::vector<float> &data, int size) {
  // Simple zero-crossing trigger (rising edge)
  // Search backwards from the most recent valid trigger point to minimize latency!
  float threshold = 0.505f; // Since zero is 0.5 in encoded visualizer buffer
  for (int i = (size / 2) - 1; i >= 1; --i) {
    if (data[i] > threshold && data[i - 1] <= threshold) {
      return i;
    }
  }
  // If no trigger is found, default to the most recent block to avoid latency
  return (size / 2) - 1;
}

// Returns the active skin colour as a JUCE Colour
juce::Colour WraithFormAudioProcessorEditor::getThemeColour() const {
  if (currentColorMode == ColorMode::UV)
    return juce::Colour(0xFF7300FF); // Deep electric UV / blacklight violet
  else if (currentColorMode == ColorMode::Infrared)
    return juce::Colour(0xFFFF0012); // True pure FLIR crimson / laser red (0% green)
  else if (currentColorMode == ColorMode::Heat)
    return juce::Colour(0xFFFF6600); // Thermal magma flame orange
  else if (currentColorMode == ColorMode::Plasma)
    return juce::Colour(0xFF1EFF10); // Radioactive isotope green
  return juce::Colour(0xFFE0F5FF);   // Default Wraith pale spectral ice mist
}

// RGB triplet for OpenGL (passed to shader or glClearColor)
void WraithFormAudioProcessorEditor::getThemeRGB(float &r, float &g,
                                                 float &b) const {
  if (currentColorMode == ColorMode::UV) {
    r = 0.45f;
    g = 0.0f;
    b = 1.0f;
  } else if (currentColorMode == ColorMode::Infrared) {
    r = 1.0f;
    g = 0.0f;
    b = 0.02f; // Pure monochromatic red; zero green contamination
  } else if (currentColorMode == ColorMode::Heat) {
    r = 1.0f;
    g = 0.40f;
    b = 0.0f;
  } else if (currentColorMode == ColorMode::Plasma) {
    r = 0.12f;
    g = 1.0f;
    b = 0.06f;
  } else {
    // Default Wraith pale ethereal mist (low saturation, luminous spectral cyan-white)
    r = 0.88f;
    g = 0.96f;
    b = 1.0f;
  }
}

void WraithFormAudioProcessorEditor::renderMeters() {
  using namespace juce::gl;

  // Smooth sidebar animation
  float target = isSideBarVisible ? 1.0f : 0.0f;
  sideBarAnimation = sideBarAnimation * 0.8f + target * 0.2f;

  if (sideBarAnimation < 0.01f)
    return;

  float scale = (float)openGLContext.getRenderingScale();
  int sidebarWidth = (int)(100 * scale);
  int x_start = (int)((getWidth() * (float)openGLContext.getRenderingScale()) -
                      (sidebarWidth * sideBarAnimation));
  int h = (int)(getHeight() * scale);

  glEnable(GL_SCISSOR_TEST);

  // Background
  glScissor(x_start, 0, sidebarWidth, h);
  glClearColor(0.0f, 0.0f, 0.0f, 0.5f); // 50% Transparent Sidebar
  glClear(GL_COLOR_BUFFER_BIT);

  // Meter Data from Processor
  float pL = audioProcessor.peakL.load();
  float pR = audioProcessor.peakR.load();
  float rL = audioProcessor.rmsL.load();
  float rR = audioProcessor.rmsR.load();
  float lufs = audioProcessor.lufsMomentary.load();

  // Peak hold logic
  peakHoldL = std::max(peakHoldL * 0.98f, pL);
  peakHoldR = std::max(peakHoldR * 0.98f, pR);
  peakHoldLUFS = std::max(peakHoldLUFS - 0.5f, lufs); // Slow decay in dB

  // Map to DB and then to 0..1 height
  auto toNorm = [](float val) {
    float db = juce::Decibels::gainToDecibels(val);
    return juce::jlimit(0.0f, 1.0f, juce::jmap(db, -60.0f, 6.0f, 0.0f, 1.0f));
  };

  float normPeakL = toNorm(pL);
  float normPeakR = toNorm(pR);
  float normRMSL = toNorm(rL);
  float normRMSR = toNorm(rR);
  // LUFS is already in DB
  float normLUFS =
      juce::jlimit(0.0f, 1.0f, juce::jmap(lufs, -60.0f, 6.0f, 0.0f, 1.0f));

  int barGap = (int)(10 * scale);
  int barW = (int)(6 * scale);
  // Total logical height H. Dashboard is 55 high. Readouts are ~100 high at
  // bottom. Meters logical height = H - 55 - 100 = H - 155.
  int meterHeight = (int)((getHeight() - 170) * scale);

  auto drawMeter = [&](int x_off, float val, bool isRMS) {
    int barH = (int)(val * meterHeight);
    // GL bottom = 115 logical pixels (clears PEAK label at ~107px)
    glScissor(x_start + x_off, (int)(115 * scale), barW, barH);
    float tr, tg, tb;
    getThemeRGB(tr, tg, tb);
    if (isRMS)
      glClearColor(tr * 0.5f, tg * 0.55f, tb * 0.55f, 0.9f);
    else
      glClearColor(tr, tg, tb, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  };

  drawMeter(barGap, normPeakL, false);
  drawMeter(barGap + barW + 2, normPeakR, false);
  drawMeter(barGap * 3, normRMSL, true);
  drawMeter(barGap * 3 + barW + 2, normRMSR, true);

  // LUFS Bar
  int lufs_off = barGap * 5;
  glDisable(GL_SCISSOR_TEST);

  // --- Dynamic Text Overlay (Numeric Readouts & Labels) ---
  std::unique_ptr<juce::LowLevelGraphicsContext> glContext(
      juce::createOpenGLGraphicsContext(openGLContext,
                                        (int)(getWidth() * scale),
                                        (int)(getHeight() * scale)));
  if (glContext != nullptr) {
    juce::Graphics g(*glContext);
    g.addTransform(juce::AffineTransform::scale(scale));

    float fontSize = 11.0f;
    g.setFont(fontSize);
    g.setColour(juce::Colours::white.withAlpha(0.8f));

    auto drawReadout = [&](int x, int y, float dbValue, const char *label) {
      juce::String text =
          (dbValue <= -99.0f) ? "-inf" : juce::String(dbValue, 1);
      int x_pos = x;
      int y_pos = y;

      g.setColour(getThemeColour().withAlpha(0.6f));
      g.setFont(9.0f);
      g.drawText(label, x_pos, y_pos - 12, 60, 10, juce::Justification::left);

      g.setColour(juce::Colours::white);
      g.setFont(12.0f);
      g.drawText(text, x_pos, y_pos, 60, 14, juce::Justification::left);
    };

    int sidebarX_log = (int)(x_start / scale);
    int textY_Top = 75; // Below dashboard

    // L/R Labels at top
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.setFont(11.0f);
    g.drawText("L", sidebarX_log + 10, textY_Top, 15, 15,
               juce::Justification::centred);
    g.drawText("R", sidebarX_log + 32, textY_Top, 15, 15,
               juce::Justification::centred);

    // Sidebar footer readouts
    float dbPeakL = juce::Decibels::gainToDecibels(pL);
    float dbPeakR = juce::Decibels::gainToDecibels(pR);
    float dbRMSL = juce::Decibels::gainToDecibels(rL);
    float dbRMSR = juce::Decibels::gainToDecibels(rR);

    // Row 1: Peak & RMS
    int row1_Y = getHeight() - 95;
    drawReadout(sidebarX_log + 10, row1_Y, std::max(dbPeakL, dbPeakR), "PEAK");
    drawReadout(sidebarX_log + 55, row1_Y, (dbRMSL + dbRMSR) * 0.5f, "RMS");

    // Row 2: LUFS
    int row2_Y = getHeight() - 45;
    drawReadout(sidebarX_log + 10, row2_Y, lufs, "LUFS");
  }
}

void WraithFormAudioProcessorEditor::renderLoudnessDashboard() {
  using namespace juce::gl;
  auto desktopScale = (float)openGLContext.getRenderingScale();
  float scale = desktopScale;
  int w = getWidth();
  int h = getHeight();
  int dashH = (int)(35 * scale);

  // We completely removed the top bar background here so the visualizer shows through 100%

  std::unique_ptr<juce::LowLevelGraphicsContext> glContext(
      juce::createOpenGLGraphicsContext(openGLContext, (int)(w * scale),
                                        (int)(h * scale)));
  if (glContext != nullptr) {
    juce::Graphics g(*glContext);
    g.addTransform(juce::AffineTransform::scale(scale));

    auto drawMetric = [&](int x, int y, const char *label, float value) {
      juce::String text = (value <= -90.0f) ? "-inf" : juce::String(value, 1);
      juce::Colour tc = getThemeColour();
      
      // Label securely tucked into the top half
      g.setColour(tc.withAlpha(0.6f));
      g.setFont(8.0f);
      g.drawText(label, x, y - 18, 120, 10, juce::Justification::bottomLeft);
      
      // Numbers elegantly tucked into the bottom half
      g.setColour(tc);
      g.setFont(16.0f);
      int tW = text.length() * 9; // Approximate width for 16pt font
      g.drawText(text, x, y - 6, tW + 5, 20, juce::Justification::topLeft);
      
      // LUFS suffix perfectly aligned
      g.setFont(8.0f);
      g.setColour(tc.withAlpha(0.4f));
      g.drawText("LUFS", x + tW + 5, y, 30, 10, juce::Justification::bottomLeft);
    };

    int startX = 20;
    int yOff = 22;
    int spacing = 150; // Increased from ~100 to 150 to prevent overlap

    drawMetric(startX, yOff, "INTEGRATED",
               audioProcessor.lufsIntegrated.load());
    drawMetric(startX + spacing, yOff, "SHORT-TERM",
               audioProcessor.lufsShortTerm.load());
    drawMetric(startX + spacing * 2, yOff, "MOMENTARY",
               audioProcessor.lufsMomentary.load());

    // True Peak Readout
    int tpX = w - 260;
    juce::Colour tc = getThemeColour();
    g.setColour(tc.withAlpha(0.6f));
    g.setFont(10.0f);
    g.drawText("TRUE PEAK (L/R)", tpX, yOff - 15, 120, 12,
               juce::Justification::left);
    g.setColour(tc);
    g.setFont(16.0f);
    juce::String tpText = juce::String(audioProcessor.truePeakL.load(), 1) +
                          " / " +
                          juce::String(audioProcessor.truePeakR.load(), 1);
    g.drawText(tpText, tpX, yOff, 120, 20, juce::Justification::left);

    // Standalone Input Display (Top Middle)
    if (audioProcessor.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
        if (audioProcessor.currentStandaloneInputName.isNotEmpty()) {
            g.setFont(10.0f);
            g.setColour(tc.withAlpha(0.4f));
            juce::String inStr = "AUDIO INPUT: " + audioProcessor.currentStandaloneInputName.toUpperCase();
            juce::Rectangle<int> centerRect(0, 5, w, 25);
            g.drawText(inStr, centerRect, juce::Justification::centred, true);
        }
    }

    // Reset Button
    int footerY = h - 35;
    juce::Rectangle<int> resetBtnRect = {20, footerY, 55, 20};
    g.setColour(juce::Colours::black);
    g.fillRect(resetBtnRect);
    g.setColour(tc.withAlpha(0.6f));
    g.setFont(juce::Font(11.0f));
    g.drawRect(resetBtnRect, 1);
    g.drawText("RESET", resetBtnRect, juce::Justification::centred);

    // Quad Button
    quadBtnRect = {85, footerY, 55, 20};
    g.setColour(juce::Colours::black);
    g.fillRect(quadBtnRect);
    g.setColour(currentMode == VisualizerMode::QuadView ? tc
                                                        : tc.withAlpha(0.6f));
    g.drawRect(quadBtnRect, 1);
    g.setFont(10.0f);
    g.drawText("MULTI", quadBtnRect, juce::Justification::centred);

    // COLOR MODE Button (Beside QUAD)
    juce::Rectangle<int> colorBtnRect = {150, footerY, 65, 20};

    // Fill background solid black
    g.setColour(juce::Colours::black);
    g.fillRect(colorBtnRect);

    // Pick swatch colour per mode
    juce::Colour swatchColor = getThemeColour();
    juce::String colorLabel;
    if (currentColorMode == ColorMode::Wraith) {
      colorLabel = "WRAITH";
    } else if (currentColorMode == ColorMode::UV) {
      colorLabel = "UV";
    } else if (currentColorMode == ColorMode::Infrared) {
      colorLabel = "INFRA";
    } else if (currentColorMode == ColorMode::Plasma) {
      colorLabel = "PLASMA";
    } else {
      colorLabel = "HEAT";
    }
    g.setColour(swatchColor.withAlpha(0.85f));
    g.drawRect(colorBtnRect, 1);
    g.setFont(10.0f);
    g.drawText(colorLabel, colorBtnRect, juce::Justification::centred);

    // ZOOM Button (Removed per user request - locked to 4x)
    /*
    if (currentMode == VisualizerMode::Oscilloscope ||
        currentMode == VisualizerMode::QuadView) {
      zoomBtnRect = {225, footerY, 70, 20};
      g.setColour(tc.withAlpha(0.6f));
      g.drawRect(zoomBtnRect, 1);
      g.setFont(10.0f);
      juce::String zoomLabel = "ZOOM X" + juce::String((int)waveformZoom);
      g.drawText(zoomLabel, zoomBtnRect, juce::Justification::centred);
    } else {
      zoomBtnRect = {}; // Inactive
    }
    */
    zoomBtnRect = {}; // Ensure always inactive

    // MULTI BUTTON REMOVED
  }
}

void WraithFormAudioProcessorEditor::newOpenGLContextCreated() {
  using namespace juce::gl;
  openGLContext.extensions.initialise();
  createShaders();

  // Setup Splash Texture
  if (splashImage.isValid()) {
    glGenTextures(1, &splashTextureID);
    glBindTexture(GL_TEXTURE_2D, splashTextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    juce::Image::BitmapData bd(splashImage, juce::Image::BitmapData::readOnly);
    // Determine format
    GLint format = bd.pixelFormat == juce::Image::RGB ? GL_RGB : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bd.width, bd.height, 0,
                 (bd.pixelStride == 3 ? GL_RGB : GL_BGRA_EXT), GL_UNSIGNED_BYTE,
                 bd.data);
  }

  // Setup Texture
  glGenTextures(1, &audioTextureID);
  glBindTexture(GL_TEXTURE_2D, audioTextureID);

  // Linear interpolation is important for smooth waveforms if we zoom in,
  // but for 1:1 mapping Nearest might be cleaner. Let's use Linear.
  // Linear interpolation is important for smooth waveforms if we zoom in,
  // but for 1:1 mapping Nearest might be cleaner. Let's use Linear.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Init FBOs
  int w = 2048; // Texture width for history - Increased for longer history
  int h = fftSize / 2; // Texture height for history (freq bins)

  // spectrogramFBO_A.reset(new juce::OpenGLFrameBuffer()); - REMOVED
  // spectrogramFBO_A->initialise(openGLContext, w, h); - REMOVED
  // spectrogramFBO_B.reset(new juce::OpenGLFrameBuffer()); - REMOVED
  // spectrogramFBO_B->initialise(openGLContext, w, h); - REMOVED

  // Clear FBOs - REMOVED (CPU Backed)
}

void WraithFormAudioProcessorEditor::renderOpenGL() {
  using namespace juce::gl;

  // ALWAYS update audio data before rendering any mode
  updateAudioData();

  // --- Update Cloud Tunnel accumulation for all modes ---
  {
    double now = juce::Time::getMillisecondCounterHiRes();
    if (highResLastFrameTime <= 0.0)
      highResLastFrameTime = now;
    float deltaT = juce::jlimit(0.0f, 0.05f,
                                (float)(now - highResLastFrameTime) / 1000.0f);
    highResLastFrameTime = now;

    float rmsNow =
        (audioProcessor.rmsL.load() + audioProcessor.rmsR.load()) * 0.5f;
    smoothedRMSLevel = smoothedRMSLevel * 0.93f + rmsNow * 0.07f;

    float rmsNorm = juce::jlimit(0.0f, 1.0f, smoothedRMSLevel * 4.0f);
    float tunnelSpeed = 0.15f + 9.85f * std::pow(rmsNorm, 2.5f);
    cloudTunnelTime += deltaT * tunnelSpeed;

    // Cosmic Flare / Fireball Speed: subtly reactive to bass energy for a
    // morphous feel
    float flareSpeed = 0.4f + 0.8f * smoothedBassEnergy;
    cosmicFlareTime += deltaT * flareSpeed;

    // Stable Time: Constant speed for non-reactive elements (like cloud hue)
    stableTime += deltaT * 1.0f;
  }

  // --- Update Audio Energy for all visualizers ---
  {
    float bassEnergy = 0.0f;
    int bgBins = std::min(12, (int)fftOutput.size());
    for (int i = 1; i < bgBins; ++i)
      bassEnergy += fftOutput[i];
    bassEnergy = juce::jlimit(0.0f, 1.0f, bassEnergy / (float)(bgBins - 1));
    // Smooth it gently so visuals don't snap too hard, but keep it snappy
    smoothedBassEnergy = smoothedBassEnergy * 0.65f + bassEnergy * 0.35f;

    // Kick and Snare energy for more peaky effects (fireball)
    float kickEnergy = 0.0f;
    int kickBins = std::min(
        15, (int)fftOutput
                .size()); // Bins 1-15 cover up to ~350Hz (snare fundamental)
    for (int i = 1; i < kickBins; ++i)
      kickEnergy = std::max(kickEnergy, fftOutput[i]);
    smoothedKickEnergy = smoothedKickEnergy * 0.5f + kickEnergy * 0.5f;
  }

  // SIDEBAR ANIMATION (Self-driven without Timer)
  if (isSideBarVisible && sideBarAnimation < 1.0f)
    sideBarAnimation = std::min(1.0f, sideBarAnimation + 0.05f);
  else if (!isSideBarVisible && sideBarAnimation > 0.0f)
    sideBarAnimation = std::max(0.0f, sideBarAnimation - 0.05f);

  // Render directly to the default framebuffer (no FBO intermediary)
  float desktopScale = (float)openGLContext.getRenderingScale();
  int w = (int)(getWidth() * desktopScale);
  int h = (int)(getHeight() * desktopScale);

  // Clear background to black
  glViewport(0, 0, w, h);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // Enable Alpha Blending
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);

  // Render active visualizer mode
  if (currentMode == VisualizerMode::Spectrogram)
    renderSpectrogram();
  else if (currentMode == VisualizerMode::Circular)
    renderCircularOscilloscope();
  else if (currentMode == VisualizerMode::Serato)
    renderSeratoWaveform();
  else if (currentMode == VisualizerMode::PhaseMeter)
    renderPhaseMeter();
  else if (currentMode == VisualizerMode::Vectorscope)
    renderVectorscope();
  else if (currentMode == VisualizerMode::CosmicFlare)
    renderCosmicFlare();
  else if (currentMode == VisualizerMode::VolumetricExplosion)
    renderVolumetricExplosion();
  else if (currentMode == VisualizerMode::QuadView)
    renderQuadView();
  else
    renderOscilloscope();

  // Draw Professional Loudness Dashboard (Top Overlay)
  renderLoudnessDashboard();

  // Draw Meters Sidebar
  renderMeters();
}

void WraithFormAudioProcessorEditor::renderCircularOscilloscope() {
  using namespace juce::gl;
  auto desktopScale = (float)openGLContext.getRenderingScale();

  if (circularShader == nullptr)
    return;

  // 1. Get initial viewport (assigned by parent)
  GLint initialVp[4];
  glGetIntegerv(GL_VIEWPORT, initialVp);
  int vw = initialVp[2];
  int vh = initialVp[3];
  
  GLint usableVp[4] = {initialVp[0], initialVp[1], initialVp[2], initialVp[3]};
  applyUsableAreaMargins(usableVp);

  float cx_usable = usableVp[0] + usableVp[2] / 2.0f;
  float cy_usable = usableVp[1] + usableVp[3] / 2.0f;
  float cx_initial = initialVp[0] + initialVp[2] / 2.0f;
  float cy_initial = initialVp[1] + initialVp[3] / 2.0f;
  
  float offsetX = (cx_usable - cx_initial) / initialVp[2];
  float offsetY = (cy_usable - cy_initial) / initialVp[3];

  // --- 1. Render CloudVortex background (opaque) ---
  if (cloudVortexShader != nullptr) {
    cloudVortexShader->use();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform2f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_resolution"),
        (GLfloat)vw, (GLfloat)vh);
    glUniform2f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_centerOffset"),
        offsetX, offsetY);

    glUniform1f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_time"),
        (GLfloat)stableTime);
    glUniform1f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_cloudT"),
        (GLfloat)cloudTunnelTime);

    glUniform1f(glGetUniformLocation(cloudVortexShader->getProgramID(),
                                     "u_audioEnergy"),
                smoothedBassEnergy);
    float tr, tg, tb;
    getThemeRGB(tr, tg, tb);
    glUniform3f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_glowColor"),
        tr, tg, tb);
    GLint cloudPos =
        glGetAttribLocation(cloudVortexShader->getProgramID(), "position");
    if (cloudPos > -1) {
      glEnableVertexAttribArray(cloudPos);
      static const GLfloat fullQuad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
      glVertexAttribPointer(cloudPos, 2, GL_FLOAT, GL_FALSE, 0, fullQuad);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glDisableVertexAttribArray(cloudPos);
    }
  }

  // Switch to ADDITIVE blend — ring and fireball glow layers on top of cloud
  glBlendFunc(GL_ONE, GL_ONE);

  // 2. Shrink Viewport to perfect square within usable area
  int usableX = usableVp[0];
  int usableY = usableVp[1];
  int usableW = usableVp[2];
  int usableH = usableVp[3];

  int sideSize = std::min(usableW, usableH);
  int xOff = (usableW - sideSize) / 2;
  int yOff = (usableH - sideSize) / 2;

  glViewport(usableX + xOff, usableY + yOff, sideSize, sideSize);

  circularShader->use();
  glUniform2f(
      glGetUniformLocation(circularShader->getProgramID(), "u_resolution"),
      (GLfloat)sideSize, (GLfloat)sideSize);

  GLint timeLoc =
      glGetUniformLocation(circularShader->getProgramID(), "u_time");
  if (timeLoc > -1)
    glUniform1f(timeLoc, (GLfloat)cosmicFlareTime);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, audioTextureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, textureSize, 1, 0, GL_RED, GL_FLOAT,
               textureData.data());

  GLint audioLoc =
      glGetUniformLocation(circularShader->getProgramID(), "u_audioData");
  if (audioLoc > -1)
    glUniform1i(audioLoc, 0);

  // Bind theme glow color
  GLint circGlowLoc =
      glGetUniformLocation(circularShader->getProgramID(), "u_glowColor");
  if (circGlowLoc > -1) {
    float tr, tg, tb;
    getThemeRGB(tr, tg, tb);
    glUniform3f(circGlowLoc, tr, tg, tb);
  }

  // Bind bass energy for radius and glow modulation
  GLint circBassLoc =
      glGetUniformLocation(circularShader->getProgramID(), "u_bassEnergy");
  if (circBassLoc > -1)
    glUniform1f(circBassLoc, smoothedBassEnergy);

  GLint positionAttribute =
      glGetAttribLocation(circularShader->getProgramID(), "position");

  if (positionAttribute > -1) {
    glEnableVertexAttribArray(positionAttribute);
    static const GLfloat vertices[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                       -1.0f, 1.0f,  1.0f, 1.0f};
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0,
                          vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(positionAttribute);
  }

  // --- Render Fireball Core (norm alpha blend = truly on top of ring) ---
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  if (fireballShader != nullptr) {
    fireballShader->use();
    glUniform2f(
        glGetUniformLocation(fireballShader->getProgramID(), "u_resolution"),
        (GLfloat)sideSize, (GLfloat)sideSize);
    glUniform1f(glGetUniformLocation(fireballShader->getProgramID(), "u_time"),
                (GLfloat)cosmicFlareTime);

    // kickEnergy: bins 1-5 (~21-107Hz) with peak-hold decay
    // — punchy transients: kick drum hits register immediately and decay slowly
    float rawKick = 0.0f;
    int kickBins = std::min(6, (int)fftOutput.size());
    for (int i = 1; i < kickBins; ++i)
      rawKick += fftOutput[i];
    rawKick = juce::jlimit(0.0f, 1.0f, rawKick / (float)(kickBins - 1));
    // Morphous EMA filter: slower attack (less snappy), very slow release
    // (morphous)
    if (rawKick > smoothedKickEnergy) {
      smoothedKickEnergy = smoothedKickEnergy * 0.85f + rawKick * 0.15f;
    } else {
      smoothedKickEnergy = smoothedKickEnergy * 0.95f + rawKick * 0.05f;
    }

    glUniform1f(
        glGetUniformLocation(fireballShader->getProgramID(), "u_audioEnergy"),
        smoothedKickEnergy);

    // Bind theme color so Shade() palette follows ColorMode
    float fbR, fbG, fbB;
    getThemeRGB(fbR, fbG, fbB);
    GLint fireGlowLoc =
        glGetUniformLocation(fireballShader->getProgramID(), "u_glowColor");
    if (fireGlowLoc > -1)
      glUniform3f(fireGlowLoc, fbR, fbG, fbB);

    GLint firePosAttr =
        glGetAttribLocation(fireballShader->getProgramID(), "position");
    if (firePosAttr > -1) {
      glEnableVertexAttribArray(firePosAttr);
      // Scale to roughly the size of the inner circle (Radius ~ 0.5)
      // Slightly larger quad to ensure no clipping of the fireball glow
      static const GLfloat fireV[] = {-0.6f, -0.6f, 0.6f, -0.6f,
                                      -0.6f, 0.6f,  0.6f, 0.6f};
      glVertexAttribPointer(firePosAttr, 2, GL_FLOAT, GL_FALSE, 0, fireV);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glDisableVertexAttribArray(firePosAttr);
    }
  }
  // Restore normal alpha blend so dashboard/meters render correctly on top
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void WraithFormAudioProcessorEditor::renderOscilloscope() {
  using namespace juce::gl;
  auto desktopScale = (float)openGLContext.getRenderingScale();

  if (oscilloscopeShader == nullptr)
    return;

  oscilloscopeShader->use();

  // Update Uniforms
  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);

  GLint resolutionLoc =
      glGetUniformLocation(oscilloscopeShader->getProgramID(), "u_resolution");
  if (resolutionLoc > -1)
    glUniform2f(resolutionLoc, (GLfloat)vp[2], (GLfloat)vp[3]);

  GLint timeLoc =
      glGetUniformLocation(oscilloscopeShader->getProgramID(), "u_time");
  if (timeLoc > -1)
    glUniform1f(timeLoc,
                (GLfloat)juce::Time::getMillisecondCounter() / 1000.0f);

  // Bind glow color based on current ColorMode via centralized helper
  GLint glowColorLoc =
      glGetUniformLocation(oscilloscopeShader->getProgramID(), "u_glowColor");
  if (glowColorLoc > -1) {
    float tr, tg, tb;
    getThemeRGB(tr, tg, tb);
    glUniform3f(glowColorLoc, tr, tg, tb);
  }

  GLint energyLoc =
      glGetUniformLocation(oscilloscopeShader->getProgramID(), "u_audioEnergy");
  if (energyLoc > -1) {
    float rms = (audioProcessor.rmsL.load() + audioProcessor.rmsR.load()) * 0.5f;
    glUniform1f(energyLoc, juce::jlimit(0.0f, 1.0f, rms * 2.5f));
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, audioTextureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, textureSize, 1, 0, GL_RED, GL_FLOAT,
               textureData.data());

  GLint audioLoc =
      glGetUniformLocation(oscilloscopeShader->getProgramID(), "u_audioData");
  if (audioLoc > -1)
    glUniform1i(audioLoc, 0); // slot 0

  // Draw Full Screen Quad
  GLint positionAttribute =
      glGetAttribLocation(oscilloscopeShader->getProgramID(), "position");

  if (positionAttribute > -1) {
    glEnableVertexAttribArray(positionAttribute);
    static const GLfloat vertices[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                       -1.0f, 1.0f,  1.0f, 1.0f};
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0,
                          vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(positionAttribute);
  }
}

void WraithFormAudioProcessorEditor::renderSpectrogram() {
  using namespace juce::gl;

  // 1. Upload Image to Texture
  if (spectrogramTextureID == 0) {
    glGenTextures(1, &spectrogramTextureID);
  }

  // Bind and Upload
  glBindTexture(GL_TEXTURE_2D, spectrogramTextureID);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // We only need to upload if we change it, but for simplicity we upload every
  // frame
  if (spectrogramImage.isValid()) {
    juce::Image::BitmapData bd(spectrogramImage,
                               juce::Image::BitmapData::readOnly);
// JUCE Images on Windows are typically BGR.
// User reported Yellow (R+G) instead of Cyan (G+B) when we sent Cyan.
// This implies R and B are swapped. Use GL_BGR (0x80E0).
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, bd.width, bd.height, 0, GL_BGR,
                 GL_UNSIGNED_BYTE, bd.data);
  }

  // 2. Render Quad
  if (spectrogramShader == nullptr)
    return;

  spectrogramShader->use();

  // Uniforms
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, spectrogramTextureID);
  GLint loc =
      glGetUniformLocation(spectrogramShader->getProgramID(), "u_texture");
  if (loc >= 0)
    glUniform1i(loc, 0);

  // Bind theme tint color
  GLint tintLoc =
      glGetUniformLocation(spectrogramShader->getProgramID(), "u_tintColor");
  if (tintLoc > -1) {
    float tr, tg, tb;
    getThemeRGB(tr, tg, tb);
    glUniform3f(tintLoc, tr, tg, tb);
  }

  // Draw
  GLint posLoc =
      glGetAttribLocation(spectrogramShader->getProgramID(), "position");
  if (posLoc > -1) {
    glEnableVertexAttribArray(posLoc);
    static const GLfloat v[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, v);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(posLoc);
  }
}

void WraithFormAudioProcessorEditor::openGLContextClosing() {
  using namespace juce::gl;
  visualizerFBO.release();
  postProcessShader = nullptr;
  oscilloscopeShader = nullptr;
  spectrogramShader = nullptr;
  glDeleteTextures(1, &audioTextureID);
  circularShader = nullptr;
  seratoShader = nullptr;
  cloudVortexShader = nullptr;
  if (waveformHistoryTexture != 0) {
    glDeleteTextures(1, &waveformHistoryTexture);
    waveformHistoryTexture = 0;
  }
  if (spectrogramTextureID != 0) {
    glDeleteTextures(1, &spectrogramTextureID);
    spectrogramTextureID = 0;
  }
}

void WraithFormAudioProcessorEditor::updateAudioData() {
  // Apply any pending FFT size changes safely on the OpenGL thread
  applyPendingFFTSize();

  // 1. Pull latest audio for Oscilloscope (using Mono sum for display)
  // We use a larger window to find a trigger point for stabilization
  const int searchSize = textureSize * 2;
  if (wideLBuffer.size() != searchSize) wideLBuffer.resize(searchSize);
  if (wideRBuffer.size() != searchSize) wideRBuffer.resize(searchSize);
  if (wideSumBuffer.size() != searchSize) wideSumBuffer.resize(searchSize);

  audioProcessor.visualizerBufferL.readHistory(wideLBuffer, searchSize);
  audioProcessor.visualizerBufferR.readHistory(wideRBuffer, searchSize);

  for (int i = 0; i < searchSize; ++i)
    wideSumBuffer[i] = (wideLBuffer[i] + wideRBuffer[i]) * 0.5f;

  int trigger = findTriggerPoint(wideSumBuffer, searchSize);

  // Apply horizontal zoom by adjusting the sampling step
  float step = 1.0f / waveformZoom;
  for (int i = 0; i < textureSize; ++i) {
    float sourceIdx = (float)i * step;
    int idx1 = (int)sourceIdx;

    // Safety clamp for interpolation lookahead
    int idx2 = idx1 + 1;
    if (trigger + idx2 >= searchSize)
      idx2 = idx1;

    float frac = sourceIdx - (float)idx1;

    // Linear interpolation for smooth zoomed visuals
    float v1 = wideSumBuffer[trigger + idx1];
    float v2 = wideSumBuffer[trigger + idx2];
    textureData[i] = v1 + (v2 - v1) * frac;
  }

  // 1.5 Populate Phase Data (Stereo)
  // Use a standard professional window: 1024 samples
  const int phasePoints = 1024;
  if (phaseData.size() != phasePoints)
    phaseData.resize(phasePoints);
  if (vectorScopeData.size() != phasePoints)
    vectorScopeData.resize(phasePoints);
  if (phaseLBuffer.size() != phasePoints)
    phaseLBuffer.resize(phasePoints);
  if (phaseRBuffer.size() != phasePoints)
    phaseRBuffer.resize(phasePoints);

  audioProcessor.visualizerBufferL.readHistory(phaseLBuffer, phasePoints);
  audioProcessor.visualizerBufferR.readHistory(phaseRBuffer, phasePoints);

  float bassL = 0.0f, bassR = 0.0f;
  for (int i = 0; i < phasePoints; ++i) {
    // Decode from [0, 1] to [-1, 1] since visualizer buffer is 0.5-centered
    float decodedL = (phaseLBuffer[i] - 0.5f) * 2.0f;
    float decodedR = (phaseRBuffer[i] - 0.5f) * 2.0f;
    float px = juce::jlimit(-0.95f, 0.95f, decodedL);
    float py = juce::jlimit(-0.95f, 0.95f, decodedR);
    phaseData[i] = juce::Point<float>(px, py);

    // Continuous low-pass filter (~300Hz) for bass-dominant vectorscope dynamics:
    // Sub-bass, 808s, and kicks pass with 100% force; harsh treble jitter is filtered out.
    bassL += 0.085f * (decodedL - bassL);
    bassR += 0.085f * (decodedR - bassR);

    // 85% low-end trajectory + 15% transient crispness with 1.35x dynamic headroom
    float vL = juce::jlimit(-0.98f, 0.98f, (bassL * 0.85f + decodedL * 0.15f) * 1.35f);
    float vR = juce::jlimit(-0.98f, 0.98f, (bassR * 0.85f + decodedR * 0.15f) * 1.35f);
    vectorScopeData[i] = juce::Point<float>(vL, vR);
  }

  // 1.6 Compute Pearson Correlation (r)
  float sumLR = 0, sumL2 = 0, sumR2 = 0;
  for (int i = 0; i < phasePoints; ++i) {
    float l = phaseData[i].x;
    float r = phaseData[i].y;
    sumLR += l * r;
    sumL2 += l * l;
    sumR2 += r * r;
  }
  float denom = std::sqrt(sumL2 * sumR2);
  currentCorrelation = (denom > 1e-6f) ? (sumLR / denom) : 0.0f;

  // Mastering-grade smoothing (~0.15 responsivity)
  smoothedCorrelation =
      smoothedCorrelation * 0.85f + currentCorrelation * 0.15f;

  // 2. FFT uses Mono sum
  int size = (int)1 << (int)currentFFTSize;
  if (forwardFFT == nullptr || windowingFunction == nullptr)
    return;

  if (fftInLBuffer.size() < (size_t)size) fftInLBuffer.resize(size);
  if (fftInRBuffer.size() < (size_t)size) fftInRBuffer.resize(size);

  audioProcessor.visualizerBufferL.readHistory(fftInLBuffer, size);
  audioProcessor.visualizerBufferR.readHistory(fftInRBuffer, size);

  if (fftBuffer.size() < (size_t)(size * 2))
    fftBuffer.resize(size * 2, 0.0f);
  if (fftOutput.size() < (size_t)(size / 2))
    fftOutput.resize(size / 2, 0.0f);

  for (int i = 0; i < size; ++i) {
    fftBuffer[i] = (fftInLBuffer[i] + fftInRBuffer[i]) * 0.5f;
  }

  windowingFunction->multiplyWithWindowingTable(fftBuffer.data(), size);
  forwardFFT->performFrequencyOnlyForwardTransform(fftBuffer.data());

  // 3. Map to Output (0..1)
  int outputSize = size / 2;
  for (int i = 0; i < outputSize; ++i) {
    if (i >= (int)fftOutput.size())
      break;

    float mag = fftBuffer[i];
    float db = juce::Decibels::gainToDecibels(mag) -
               juce::Decibels::gainToDecibels((float)(outputSize * 2));
    float norm = juce::jmap(db, -90.0f, 0.0f, 0.0f, 1.0f);
    fftOutput[i] = juce::jlimit(0.0f, 1.0f, norm);
  }

  // 4. Update Spectrogram Image (CPU Shift) - only when Spectrogram or QuadView is active!
  bool isSpectrogramActive = (currentMode == VisualizerMode::Spectrogram ||
                              currentMode == VisualizerMode::QuadView);
  if (isSpectrogramActive && spectrogramImage.isValid()) {
    spectrogramImage.moveImageSection(0, 0, 1, 0,
                                      spectrogramImage.getWidth() - 1,
                                      spectrogramImage.getHeight());

    // Draw new column at width-1
    int w = spectrogramImage.getWidth();
    int h = spectrogramImage.getHeight();
    int x = w - 1;

    juce::Image::BitmapData bd(spectrogramImage,
                               juce::Image::BitmapData::readWrite);
    for (int y = 0; y < h; ++y) {
      float normalizedY = 1.0f - (float)y / (float)h;
      int bin = (int)(normalizedY * outputSize);
      if (bin >= outputSize)
        bin = outputSize - 1;
      if (bin < 0)
        bin = 0;

      float val = (bin < (int)fftOutput.size()) ? fftOutput[bin] : 0.0f;
      val = pow(val, 0.78f);

      juce::Colour col = juce::Colour(0xFF000000);
      if (val > 0.01f) {
        juce::uint8 lum = (juce::uint8)juce::jlimit(0.0f, 255.0f, val * 255.0f);
        col = juce::Colour(lum, lum, lum);
      }
      bd.setPixelColour(x, y, col);
    }
  }
}

void WraithFormAudioProcessorEditor::renderSeratoWaveform() {
  using namespace juce::gl;
  auto desktopScale = (float)openGLContext.getRenderingScale();

  if (seratoShader == nullptr)
    return;

  // 1. Synchronize Multiband History from Processor
  int writeIdx = audioProcessor.waveformHistoryWriteIndex.load();
  int size = audioProcessor.waveformHistorySize;

  // Reorder cyclic buffer to linear for texture (scrolling effect)
  for (int i = 0; i < size; ++i) {
    waveformHistory[i] = audioProcessor.waveformHistory[(writeIdx + i) % size];
  }

  // 2. Upload History Texture
  if (waveformHistoryTexture == 0) {
    glGenTextures(1, &waveformHistoryTexture);
  }
  glBindTexture(GL_TEXTURE_2D, waveformHistoryTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, historySize, 1, 0, GL_RGBA,
               GL_FLOAT, waveformHistory.data());

  // 3. Draw
  seratoShader->use();
  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);
  glUniform2f(
      glGetUniformLocation(seratoShader->getProgramID(), "u_resolution"),
      (GLfloat)vp[2], (GLfloat)vp[3]);
  glUniform1f(glGetUniformLocation(seratoShader->getProgramID(), "u_time"),
              (float)cosmicFlareTime);

  // PASS SCALING LOGIC
  // Zoom level 1.0f shows the full 5-second buffer history. 
  // Previously this was 2.0f, causing it to whip across the screen too fast and compress the dynamics.
  float seratoZoom = 1.0f;
  glUniform1f(glGetUniformLocation(seratoShader->getProgramID(), "u_zoom"),
              seratoZoom);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, waveformHistoryTexture);
  glUniform1i(glGetUniformLocation(seratoShader->getProgramID(), "u_history"),
              0);

  // Bind theme color
  float tr, tg, tb;
  getThemeRGB(tr, tg, tb);
  GLint seratoGlowLoc =
      glGetUniformLocation(seratoShader->getProgramID(), "u_glowColor");
  if (seratoGlowLoc > -1)
    glUniform3f(seratoGlowLoc, tr, tg, tb);

  GLint posLoc = glGetAttribLocation(seratoShader->getProgramID(), "position");
  if (posLoc > -1) {
    glEnableVertexAttribArray(posLoc);
    static const GLfloat v[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, v);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(posLoc);
  }
}

void WraithFormAudioProcessorEditor::renderQuadView() {
  using namespace juce::gl;
  int w = getWidth();
  int h = getHeight();
  float desktopScale = (float)openGLContext.getRenderingScale();

  int dashH = (int)(35 * desktopScale);
  int availableH = h * desktopScale - dashH; // Exclude top bar

  int halfH = availableH / 2;
  int thirdW = (int)((w * desktopScale) / 3);
  int halfW = (int)((w * desktopScale) / 2);

  // Layout:
  // Top Row (3 items): Oscilloscope, Circular, Phase Meter
  // Bottom Row (2 items): Spectrogram, Serato

  // 1. Oscilloscope (Top-Left)
  glViewport(0, halfH, thirdW, halfH);
  renderOscilloscope();

  // 2. Circular (Top-Center)
  glViewport(thirdW, halfH, thirdW, halfH);
  renderCircularOscilloscope();

  // 3. Phase Meter (Top-Right)
  glViewport(thirdW * 2, halfH, thirdW, halfH);
  renderPhaseMeter();

  // 4. Spectrogram (Bottom-Left)
  glViewport(0, 0, halfW, halfH);
  renderSpectrogram();

  // 5. Serato (Bottom-Right)
  glViewport(halfW, 0, halfW, halfH);
  renderSeratoWaveform();

  // Restore full viewport for overlays
  glViewport(0, 0, (int)(w * desktopScale), (int)(h * desktopScale));
}

void WraithFormAudioProcessorEditor::renderPhaseMeter() {
  using namespace juce::gl;

  if (phaseShader == nullptr)
    return;

  // 1. Get initial viewport (either full window or a Quadrant)
  GLint initialVp[4];
  glGetIntegerv(GL_VIEWPORT, initialVp);
  
  GLint usableVp[4] = {initialVp[0], initialVp[1], initialVp[2], initialVp[3]};
  applyUsableAreaMargins(usableVp);

  float cx_usable = usableVp[0] + usableVp[2] / 2.0f;
  float cy_usable = usableVp[1] + usableVp[3] / 2.0f;
  float cx_initial = initialVp[0] + initialVp[2] / 2.0f;
  float cy_initial = initialVp[1] + initialVp[3] / 2.0f;
  float offsetX = (cx_usable - cx_initial) / initialVp[2];
  float offsetY = (cy_usable - cy_initial) / initialVp[3];

  // --- 0. Persistence Fade (Fill viewport) ---
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(0.0f, 0.0f, 0.0f, 0.15f);
  glRectf(-1.0f, -1.0f, 1.0f, 1.0f);

  // --- 1. Background (Fill initial parent viewport) ---
  if (cloudVortexShader != nullptr) {
    glDisable(GL_CULL_FACE);
    cloudVortexShader->use();
    glUniform1f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_time"),
        (GLfloat)stableTime);
    glUniform2f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_resolution"),
        (GLfloat)initialVp[2], (GLfloat)initialVp[3]);
    glUniform2f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_centerOffset"),
        offsetX, offsetY);
    glUniform1f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_cloudT"),
        cloudTunnelTime);

    float bassEnergy =
        (audioProcessor.rmsL.load() + audioProcessor.rmsR.load()) * 0.5f;
    glUniform1f(glGetUniformLocation(cloudVortexShader->getProgramID(),
                                     "u_audioEnergy"),
                juce::jlimit(0.0f, 1.0f, bassEnergy * 2.0f));

    float tr, tg, tb;
    getThemeRGB(tr, tg, tb);
    glUniform3f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_glowColor"),
        tr, tg, tb);

    static const GLfloat qV[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                 -1.0f, 1.0f,  1.0f, 1.0f};
    GLint pos =
        glGetAttribLocation(cloudVortexShader->getProgramID(), "position");
    if (pos > -1) {
      glEnableVertexAttribArray(pos);
      glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0, qV);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glDisableVertexAttribArray(pos);
    }
  }

  int usableX = usableVp[0];
  int usableY = usableVp[1];
  int usableW = usableVp[2];
  int usableH = usableVp[3];

  int sideSize = std::min(usableW, usableH);
  int xOff = (usableW - sideSize) / 2;
  int yOff = (usableH - sideSize) / 2;

  glViewport(usableX + xOff, usableY + yOff, sideSize, sideSize);

  phaseShader->use();
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glEnable(0x8861); // GL_POINT_SPRITE
  glPointSize(24.0f);

  // CRITICAL: Resolution uniform must match the PLOT viewport (Square)
  glUniform2f(glGetUniformLocation(phaseShader->getProgramID(), "u_resolution"),
              (GLfloat)sideSize, (GLfloat)sideSize);
  glUniform1f(glGetUniformLocation(phaseShader->getProgramID(), "scale"), 1.0f);

  float tr, tg, tb;
  getThemeRGB(tr, tg, tb);
  glUniform4f(glGetUniformLocation(phaseShader->getProgramID(), "color"), tr,
              tg, tb, 0.85f);

  // Data Selection
  const juce::Point<float> *dataPtr = nullptr;
  int dataSize = 0;

  // Safe Mode Ring (Visualizes even when no audio is present)
  static std::vector<juce::Point<float>> safeRing;
  if (safeRing.empty()) {
    for (float a = 0.0f; a < 6.283f; a += 0.05f)
      safeRing.push_back({std::sin(a) * 0.5f, std::cos(a) * 0.5f});
  }

  if (phaseData.size() > 0) {
    dataPtr = phaseData.data();
    dataSize = (int)phaseData.size();
  } else {
    dataPtr = safeRing.data();
    dataSize = (int)safeRing.size();
  }

  if (dataSize > 0) {
    glPointSize(4.0f);
    GLint posLoc = glGetAttribLocation(phaseShader->getProgramID(), "position");
    if (posLoc > -1) {
      glEnableVertexAttribArray(posLoc);
      glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, dataPtr);
      glDrawArrays(GL_POINTS, 0, dataSize);
      glDisableVertexAttribArray(posLoc);
    }
  }

  // --- 2. Render Correlation Bar Overlay ---
  auto desktopScale = (float)openGLContext.getRenderingScale();
  int fullW = (int)(getWidth() * desktopScale);
  int fullH = (int)(getHeight() * desktopScale);

  std::unique_ptr<juce::LowLevelGraphicsContext> glContext(
      juce::createOpenGLGraphicsContext(openGLContext, fullW, fullH));
  if (glContext != nullptr) {
    juce::Graphics g(*glContext);
    g.addTransform(juce::AffineTransform::scale(desktopScale));

    int w = getWidth();
    int h = getHeight();
    int logSideM = (int)(100 * sideBarAnimation);
    int barW = 320;
    int barH = 10;
    int barX =
        (w - logSideM - barW) / 2; // Subtract sidebar width from centering
    int barY = h - 75;

    // Draw L/R Background Markings
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.setFont(24.0f);
    int availW = w - logSideM;
    g.drawText("L", 40, h / 2 - 50, 100, 100, juce::Justification::centred);
    g.drawText("R", availW - 140, h / 2 - 50, 100, 100,
               juce::Justification::centred);

    // Bar Background
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle((float)barX, (float)barY, (float)barW, (float)barH,
                           3.0f);
    g.setColour(getThemeColour().withAlpha(0.2f));
    g.drawRoundedRectangle((float)barX, (float)barY, (float)barW, (float)barH,
                           3.0f, 1.0f);

    // Center Tick (Mono Point)
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.drawVerticalLine(barX + barW / 2, (float)barY, (float)(barY + barH));

    // Correlation Fill
    float corr = smoothedCorrelation; // -1 to 1
    float fillWidth = std::abs(corr * (barW / 2.0f));
    float fillStartX = (corr >= 0) ? (float)(barX + barW / 2) : (float)(barX + barW / 2) - fillWidth;

    juce::Colour corrCol = getThemeColour();
    g.setColour(corrCol.withAlpha(0.8f));
    g.fillRect(juce::Rectangle<float>(fillStartX, (float)barY + 2, fillWidth,
                                      (float)barH - 4));

    // Labels
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.setFont(11.0f);
    g.drawText("-1", barX - 30, barY - 2, 25, barH + 4,
               juce::Justification::centredRight);
    g.drawText("0", barX + barW / 2 - 10, barY - 14, 20, 12,
               juce::Justification::centred);
    g.drawText("+1", barX + barW + 5, barY - 2, 25, barH + 4,
               juce::Justification::centredLeft);

    g.setFont(juce::Font(10.0f));
    g.setColour(getThemeColour().withAlpha(0.6f));
    g.drawText("PHASE CORRELATION", barX, barY + 14, barW, 12,
               juce::Justification::centred);
  }
}

void WraithFormAudioProcessorEditor::renderVectorscope() {
  using namespace juce::gl;

  if (vectorscopeShader == nullptr)
    return;

  // 1. Get initial viewport (assigned by parent)
  GLint initialVp[4];
  glGetIntegerv(GL_VIEWPORT, initialVp);
  float ds = (float)openGLContext.getRenderingScale();

  GLint usableVp[4] = {initialVp[0], initialVp[1], initialVp[2], initialVp[3]};
  applyUsableAreaMargins(usableVp);

  // Clear entire background to deep obsidian CRT black
  glViewport(initialVp[0], initialVp[1], initialVp[2], initialVp[3]);
  glClearColor(0.008f, 0.010f, 0.014f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // 2. Setup centered square plotting viewport within usable area
  int usableX = usableVp[0];
  int usableY = usableVp[1];
  int usableW = usableVp[2];
  int usableH = usableVp[3];

  int sideSize = std::min(usableW, usableH);
  int xOff = (usableW - sideSize) / 2;
  int yOff = (usableH - sideSize) / 2;
  int plotX = usableX + xOff;
  int plotY = usableY + yOff;

  glViewport(plotX, plotY, sideSize, sideSize);

  // 3. Render CRT Oscilloscope Screen Faceplate
  if (vectorscopeGlowShader != nullptr) {
    vectorscopeGlowShader->use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUniform2f(glGetUniformLocation(vectorscopeGlowShader->getProgramID(), "u_resolution"),
                (GLfloat)sideSize, (GLfloat)sideSize);

    float bassDrive = std::max(smoothedBassEnergy, smoothedKickEnergy);
    glUniform1f(glGetUniformLocation(vectorscopeGlowShader->getProgramID(), "u_audioEnergy"),
                juce::jlimit(0.0f, 1.0f, bassDrive * 2.2f));

    float tr, tg, tb;
    getThemeRGB(tr, tg, tb);
    glUniform3f(glGetUniformLocation(vectorscopeGlowShader->getProgramID(), "u_glowColor"), tr, tg, tb);

    static const GLfloat qV[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                 -1.0f, 1.0f,  1.0f, 1.0f};
    GLint pos = glGetAttribLocation(vectorscopeGlowShader->getProgramID(), "position");
    if (pos > -1) {
      glEnableVertexAttribArray(pos);
      glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0, qV);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glDisableVertexAttribArray(pos);
    }
  }

  // 4. Render Lissajous Beam Trace (Connected GL_LINE_STRIP + Phosphor Accumulation)
  vectorscopeShader->use();

  glEnable(GL_BLEND);
  // Additive blending: beam overlaps burn bright into phosphor saturation
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);

  glUniform2f(
      glGetUniformLocation(vectorscopeShader->getProgramID(), "u_resolution"),
      (GLfloat)sideSize, (GLfloat)sideSize);
  glUniform1f(glGetUniformLocation(vectorscopeShader->getProgramID(), "scale"),
              1.0f);
  GLint modeLoc = glGetUniformLocation(vectorscopeShader->getProgramID(), "u_plotMode");
  if (modeLoc > -1)
    glUniform1i(modeLoc, isLissajousXY ? 1 : 0);

  glUniform1f(glGetUniformLocation(vectorscopeShader->getProgramID(), "u_bassEnergy"),
              smoothedBassEnergy);
  glUniform1f(glGetUniformLocation(vectorscopeShader->getProgramID(), "u_kickEnergy"),
              smoothedKickEnergy);

  float tr, tg, tb;
  getThemeRGB(tr, tg, tb);

  GLint posLoc =
      glGetAttribLocation(vectorscopeShader->getProgramID(), "position");

  int numSamples = (int)vectorScopeData.size();
  if (posLoc > -1 && numSamples > 1) {
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, vectorScopeData.data());

    // Dynamic phosphor beam widths driven directly by bass & kick hits
    float glowLineWidth = 3.0f + smoothedBassEnergy * 3.5f + smoothedKickEnergy * 2.0f;
    float coreLineWidth = 1.2f + smoothedBassEnergy * 0.8f;
    float glowAlpha     = 0.28f + smoothedBassEnergy * 0.25f;

    // Pass 1: Diffuse Phosphor Glow Sheath (Thick line, swells on bass hits)
    glUniform4f(glGetUniformLocation(vectorscopeShader->getProgramID(), "color"),
                tr * (1.3f + smoothedBassEnergy * 0.8f),
                tg * (1.3f + smoothedBassEnergy * 0.8f),
                tb * (1.3f + smoothedBassEnergy * 0.8f),
                glowAlpha);
    glLineWidth(glowLineWidth);
    glDrawArrays(GL_LINE_STRIP, 0, numSamples);

    // Pass 2: Sharp Electron Core Beam (Thin line, intense luminous core)
    float coreR = juce::jmin(1.0f, tr * 2.0f + 0.35f + smoothedKickEnergy * 0.2f);
    float coreG = juce::jmin(1.0f, tg * 2.0f + 0.35f + smoothedKickEnergy * 0.2f);
    float coreB = juce::jmin(1.0f, tb * 2.0f + 0.35f + smoothedKickEnergy * 0.2f);
    glUniform4f(glGetUniformLocation(vectorscopeShader->getProgramID(), "color"),
                coreR, coreG, coreB, 0.90f);
    glLineWidth(coreLineWidth);
    glDrawArrays(GL_LINE_STRIP, 0, numSamples);

    // Pass 3: Phosphor Nodes (Point accumulation where signal lingers)
    glPointSize(2.0f + smoothedBassEnergy * 2.5f);
    glUniform4f(glGetUniformLocation(vectorscopeShader->getProgramID(), "color"),
                tr * 1.6f, tg * 1.6f, tb * 1.6f, 0.20f + smoothedBassEnergy * 0.20f);
    glDrawArrays(GL_POINTS, 0, numSamples);

    glLineWidth(1.0f);
    glDisableVertexAttribArray(posLoc);
  }

  // Restore normal alpha blending for UI overlays
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // 5. Render Calibrated Scope Graticule & Reticle HUD (JUCE Graphics)
  int fullW = (int)(getWidth() * ds);
  int fullH = (int)(getHeight() * ds);

  std::unique_ptr<juce::LowLevelGraphicsContext> glContext(
      juce::createOpenGLGraphicsContext(openGLContext, fullW, fullH));
  if (glContext != nullptr) {
    juce::Graphics g(*glContext);
    g.addTransform(juce::AffineTransform::scale(ds));

    // Convert OpenGL physical pixel center to JUCE logical pixel coordinates
    float logicalCx = (float)(plotX + sideSize / 2) / ds;
    float logicalCy = (float)getHeight() - ((float)(plotY + sideSize / 2) / ds);
    float R = ((float)sideSize / (2.0f * ds)) * 0.80f;

    juce::Colour themeCol = getThemeColour();

    // A. Concentric Circles (0 dB outer boundary and -6 dB inner reference)
    g.setColour(themeCol.withAlpha(0.35f));
    g.drawEllipse(logicalCx - R, logicalCy - R, R * 2.0f, R * 2.0f, 1.2f);

    float r6 = R * 0.501187f; // -6 dB
    g.setColour(themeCol.withAlpha(0.18f));
    g.drawEllipse(logicalCx - r6, logicalCy - r6, r6 * 2.0f, r6 * 2.0f, 1.0f);

    // B. Cardinal Axes with Precision Ticks
    g.setColour(themeCol.withAlpha(0.28f));
    g.drawLine(logicalCx, logicalCy - R * 1.06f, logicalCx, logicalCy + R * 1.06f, 1.0f);
    g.drawLine(logicalCx - R * 1.06f, logicalCy, logicalCx + R * 1.06f, logicalCy, 1.0f);

    // Axis Ticks at 25%, 50%, 75%, 100% radius
    for (float frac : {0.25f, 0.50f, 0.75f, 1.0f}) {
      float d = R * frac;
      g.drawLine(logicalCx - 3.0f, logicalCy - d, logicalCx + 3.0f, logicalCy - d, 1.0f);
      g.drawLine(logicalCx - 3.0f, logicalCy + d, logicalCx + 3.0f, logicalCy + d, 1.0f);
      g.drawLine(logicalCx - d, logicalCy - 3.0f, logicalCx - d, logicalCy + 3.0f, 1.0f);
      g.drawLine(logicalCx + d, logicalCy - 3.0f, logicalCx + d, logicalCy + 3.0f, 1.0f);
    }

    // C. Diagonal 45-Degree Reference Axes
    float diag = R * 0.70710678f;
    g.setColour(themeCol.withAlpha(0.22f));
    g.drawLine(logicalCx - diag * 1.04f, logicalCy - diag * 1.04f,
               logicalCx + diag * 1.04f, logicalCy + diag * 1.04f, 1.0f);
    g.drawLine(logicalCx + diag * 1.04f, logicalCy - diag * 1.04f,
               logicalCx - diag * 1.04f, logicalCy + diag * 1.04f, 1.0f);

    // D. Center Crosshair Reticle
    g.setColour(themeCol.withAlpha(0.50f));
    g.drawEllipse(logicalCx - 3.0f, logicalCy - 3.0f, 6.0f, 6.0f, 1.0f);

    // E. Reticle Labels based on Mode
    g.setFont(juce::Font(11.0f, juce::Font::bold));

    if (!isLissajousXY) {
      // --- GONIOMETER (M/S EXPANDED) ---
      // Top: Mid / In-Phase
      g.setColour(juce::Colours::white.withAlpha(0.85f));
      g.drawText("+M", (int)(logicalCx - 20.0f), (int)(logicalCy - R - 22.0f), 40, 16,
                 juce::Justification::centred);

      // Bottom: -M
      g.setColour(juce::Colours::white.withAlpha(0.45f));
      g.drawText("-M", (int)(logicalCx - 20.0f), (int)(logicalCy + R + 6.0f), 40, 16,
                 juce::Justification::centred);

      // Top-Left: +L Channel
      g.setColour(juce::Colours::white.withAlpha(0.85f));
      g.drawText("+L", (int)(logicalCx - diag - 32.0f), (int)(logicalCy - diag - 18.0f), 30, 16,
                 juce::Justification::centred);

      // Top-Right: +R Channel
      g.drawText("+R", (int)(logicalCx + diag + 2.0f), (int)(logicalCy - diag - 18.0f), 30, 16,
                 juce::Justification::centred);

      // Sides: Stereo Difference
      g.setColour(juce::Colours::white.withAlpha(0.45f));
      g.drawText("-S", (int)(logicalCx - R - 35.0f), (int)(logicalCy - 8.0f), 30, 16,
                 juce::Justification::centredRight);
      g.drawText("+S", (int)(logicalCx + R + 5.0f), (int)(logicalCy - 8.0f), 30, 16,
                 juce::Justification::centredLeft);
    } else {
      // --- CLASSIC LISSAJOUS (X: LEFT, Y: RIGHT) ---
      // Top: Right Channel (+Y)
      g.setColour(juce::Colours::white.withAlpha(0.85f));
      g.drawText("+R", (int)(logicalCx - 20.0f), (int)(logicalCy - R - 22.0f), 40, 16,
                 juce::Justification::centred);

      // Bottom: -R
      g.setColour(juce::Colours::white.withAlpha(0.45f));
      g.drawText("-R", (int)(logicalCx - 20.0f), (int)(logicalCy + R + 6.0f), 40, 16,
                 juce::Justification::centred);

      // Right: Left Channel (+X)
      g.setColour(juce::Colours::white.withAlpha(0.85f));
      g.drawText("+L", (int)(logicalCx + R + 5.0f), (int)(logicalCy - 8.0f), 30, 16,
                 juce::Justification::centredLeft);

      // Left: -L
      g.setColour(juce::Colours::white.withAlpha(0.45f));
      g.drawText("-L", (int)(logicalCx - R - 35.0f), (int)(logicalCy - 8.0f), 30, 16,
                 juce::Justification::centredRight);

      // 45 deg diagonal: MONO (L = R)
      g.drawText("MONO", (int)(logicalCx + diag + 2.0f), (int)(logicalCy - diag - 18.0f), 45, 16,
                 juce::Justification::centredLeft);
    }

    // Scale ring readouts
    g.setFont(juce::Font(9.0f));
    g.setColour(themeCol.withAlpha(0.45f));
    g.drawText("0 dB", (int)(logicalCx + 5.0f), (int)(logicalCy - R + 2.0f), 30, 12,
               juce::Justification::left);
    g.drawText("-6 dB", (int)(logicalCx + 5.0f), (int)(logicalCy - r6 + 2.0f), 30, 12,
               juce::Justification::left);

    // F. Bottom Status HUD: Mode & Stereo Phase Correlation
    float corr = smoothedCorrelation;
    juce::String corrStr = (corr >= 0.0f ? "+" : "") + juce::String(corr, 2);
    juce::String modeName = isLissajousXY ? "LISSAJOUS (L/R)" : "GONIOMETER (M/S 2.2X)";
    juce::String statusStr = "VECTORSCOPE: " + modeName + "  |  CORRELATION: " + corrStr;
    if (corr > 0.85f) statusStr += " (MONO)";
    else if (corr > 0.35f) statusStr += " (STEREO)";
    else if (corr >= -0.15f) statusStr += " (WIDE)";
    else statusStr += " (PHASE INVERTED)";

    statusStr += "  [RIGHT-CLICK TO TOGGLE]";

    g.setFont(juce::Font(10.0f));
    g.setColour(themeCol.withAlpha(0.70f));
    g.drawText(statusStr, (int)(logicalCx - 250.0f), (int)(logicalCy + R + 26.0f), 500, 16,
               juce::Justification::centred);
  }

  // Restore full viewport for parent
  glViewport(0, 0, (int)(getWidth() * ds), (int)(getHeight() * ds));
}

void WraithFormAudioProcessorEditor::renderCosmicFlare() {
  using namespace juce::gl;
  if (cosmicFlareShader == nullptr)
    return;

  GLint initialVp[4];
  glGetIntegerv(GL_VIEWPORT, initialVp);
  
  GLint usableVp[4] = {initialVp[0], initialVp[1], initialVp[2], initialVp[3]};
  applyUsableAreaMargins(usableVp);

  float cx_usable = usableVp[0] + usableVp[2] / 2.0f;
  float cy_usable = usableVp[1] + usableVp[3] / 2.0f;
  float cx_initial = initialVp[0] + initialVp[2] / 2.0f;
  float cy_initial = initialVp[1] + initialVp[3] / 2.0f;
  
  float offsetX = (cx_usable - cx_initial) / initialVp[2];
  float offsetY = (cy_usable - cy_initial) / initialVp[3];

  glViewport(initialVp[0], initialVp[1], initialVp[2], initialVp[3]);

  cosmicFlareShader->use();
  cosmicFlareShader->setUniform("u_time", cosmicFlareTime);
  cosmicFlareShader->setUniform("u_resolution", (float)initialVp[2], (float)initialVp[3]);
  cosmicFlareShader->setUniform("u_centerOffset", offsetX, offsetY);
  cosmicFlareShader->setUniform("u_audioEnergy", smoothedBassEnergy);

  float tr, tg, tb;
  getThemeRGB(tr, tg, tb);
  cosmicFlareShader->setUniform("u_glowColor", tr, tg, tb);

  // Setup Quad Geometry for Full Screen Shader
  static const GLfloat qVerts[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                   -1.0f, 1.0f,  1.0f, 1.0f};

  GLint posAttrib = openGLContext.extensions.glGetAttribLocation(
      cosmicFlareShader->getProgramID(), "position");
  if (posAttrib > -1) {
    openGLContext.extensions.glEnableVertexAttribArray(posAttrib);
    openGLContext.extensions.glVertexAttribPointer(posAttrib, 2, GL_FLOAT,
                                                   GL_FALSE, 0, qVerts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    openGLContext.extensions.glDisableVertexAttribArray(posAttrib);
  }
}

void WraithFormAudioProcessorEditor::renderVolumetricExplosion() {
  using namespace juce::gl;

  GLint initialVp[4];
  glGetIntegerv(GL_VIEWPORT, initialVp);
  
  GLint usableVp[4] = {initialVp[0], initialVp[1], initialVp[2], initialVp[3]};
  applyUsableAreaMargins(usableVp);

  float cx_usable = usableVp[0] + usableVp[2] / 2.0f;
  float cy_usable = usableVp[1] + usableVp[3] / 2.0f;
  float cx_initial = initialVp[0] + initialVp[2] / 2.0f;
  float cy_initial = initialVp[1] + initialVp[3] / 2.0f;
  
  float offsetX = (cx_usable - cx_initial) / initialVp[2];
  float offsetY = (cy_usable - cy_initial) / initialVp[3];

  glViewport(initialVp[0], initialVp[1], initialVp[2], initialVp[3]);

  // Setup Geometry
  static const GLfloat qVerts[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                   -1.0f, 1.0f,  1.0f, 1.0f};

  // Draw Liquid Fire Background
  if (liquidFireShader != nullptr) {
    liquidFireShader->use();
    liquidFireShader->setUniform("u_time", (float)stableTime);
    liquidFireShader->setUniform("u_resolution", (float)initialVp[2], (float)initialVp[3]);
    liquidFireShader->setUniform("u_centerOffset", offsetX, offsetY);
    liquidFireShader->setUniform("u_audioEnergy", smoothedBassEnergy);
    float tr, tg, tb;
    getThemeRGB(tr, tg, tb);
    liquidFireShader->setUniform("u_glowColor", tr, tg, tb);

    GLint posAttr = openGLContext.extensions.glGetAttribLocation(
        liquidFireShader->getProgramID(), "position");
    if (posAttr > -1) {
      openGLContext.extensions.glEnableVertexAttribArray(posAttr);
      openGLContext.extensions.glVertexAttribPointer(posAttr, 2, GL_FLOAT,
                                                     GL_FALSE, 0, qVerts);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      openGLContext.extensions.glDisableVertexAttribArray(posAttr);
    }
  }

  // Draw Ethereal Flame on top
  if (volumetricExplosionShader == nullptr)
    return;

  // Enable Additive Blending so it sits beautifully on the liquid fire
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  volumetricExplosionShader->use();
  volumetricExplosionShader->setUniform("u_time", (float)stableTime);
  volumetricExplosionShader->setUniform("u_resolution", (float)initialVp[2], (float)initialVp[3]);
  volumetricExplosionShader->setUniform("u_centerOffset", offsetX, offsetY);
  volumetricExplosionShader->setUniform("u_audioEnergy", smoothedBassEnergy);
  volumetricExplosionShader->setUniform("u_kickEnergy", smoothedKickEnergy);
  volumetricExplosionShader->setUniform("u_cloudT", (GLfloat)cloudTunnelTime);

  float tr, tg, tb;
  getThemeRGB(tr, tg, tb);
  volumetricExplosionShader->setUniform("u_glowColor", tr, tg, tb);

  GLint posAttrib = openGLContext.extensions.glGetAttribLocation(
      volumetricExplosionShader->getProgramID(), "position");
  if (posAttrib > -1) {
    openGLContext.extensions.glEnableVertexAttribArray(posAttrib);
    openGLContext.extensions.glVertexAttribPointer(posAttrib, 2, GL_FLOAT,
                                                   GL_FALSE, 0, qVerts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    openGLContext.extensions.glDisableVertexAttribArray(posAttrib);
  }

  // Restore normal alpha blend so UI overlays draw correctly
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void WraithFormAudioProcessorEditor::createShaders() {
  // Vertex shader — GLSL 1.30 core (OpenGL 3.2+ compatible)
  juce::String vertexShader = R"(
        #version 130
        in vec2 position;
        out vec2 v_uv;
        void main()
        {
            gl_Position = vec4(position, 0.0, 1.0);
            v_uv = position * 0.5 + 0.5;
        }
    )";

  // Post Processing Shader (Chromatic Aberration) — GLSL 1.30
  juce::String postProcessFragmentShader = R"(
      #version 130
      uniform sampler2D u_image;
      uniform float u_aberrationAmount;
      in vec2 v_uv;
      out vec4 fragColor;
      void main() {
          vec2 uv = v_uv;
          vec2 center = vec2(0.5, 0.5);
          float dist = distance(uv, center);
          float amount = u_aberrationAmount * dist * 1.5;
          vec4 col;
          col.r = texture(u_image, vec2(uv.x + amount, uv.y)).r;
          col.g = texture(u_image, uv).g;
          col.b = texture(u_image, vec2(uv.x - amount, uv.y)).b;
          col.a = texture(u_image, uv).a;
          fragColor = col;
      }
  )";

  std::unique_ptr<juce::OpenGLShaderProgram> ppShader(
      new juce::OpenGLShaderProgram(openGLContext));
  if (ppShader->addVertexShader(vertexShader) &&
      ppShader->addFragmentShader(postProcessFragmentShader) &&
      ppShader->link()) {
    postProcessShader = std::move(ppShader);
  } else {
    DBG("postProcessShader FAILED: " + ppShader->getLastError());
  }

  // Oscilloscope Shader
  std::unique_ptr<juce::OpenGLShaderProgram> oscShader(
      new juce::OpenGLShaderProgram(openGLContext));
  if (oscShader->addVertexShader(vertexShader) &&
      oscShader->addFragmentShader(visualizerFragmentShader) &&
      oscShader->link()) {
    oscilloscopeShader = std::move(oscShader);
  }

  // Splash Shader
  std::unique_ptr<juce::OpenGLShaderProgram> sShader(
      new juce::OpenGLShaderProgram(openGLContext));
  juce::String splashFragmentShader = R"(
    #version 130
    in vec2 v_uv;
    out vec4 fragColor;
    uniform sampler2D u_image;
    uniform float u_alpha;
    void main()
    {
        vec4 color = texture(u_image, v_uv);
        fragColor = vec4(color.rgb, color.a * u_alpha);
    }
  )";
  if (sShader->addVertexShader(vertexShader) &&
      sShader->addFragmentShader(splashFragmentShader) && sShader->link()) {
    splashShader = std::move(sShader);
  }

  // Spectrogram Shader
  std::unique_ptr<juce::OpenGLShaderProgram> specShader(
      new juce::OpenGLShaderProgram(openGLContext));
  if (specShader->addVertexShader(vertexShader) &&
      specShader->addFragmentShader(spectrogramFragmentShader) &&
      specShader->link()) {
    spectrogramShader = std::move(specShader);
  }

  // Circular Oscilloscope Shader
  std::unique_ptr<juce::OpenGLShaderProgram> circShader(
      new juce::OpenGLShaderProgram(openGLContext));
  if (circShader->addVertexShader(vertexShader) &&
      circShader->addFragmentShader(circularOscilloscopeFragmentShader) &&
      circShader->link()) {
    circularShader = std::move(circShader);
  }

  // Serato Shader
  std::unique_ptr<juce::OpenGLShaderProgram> seratoS(
      new juce::OpenGLShaderProgram(openGLContext));
  if (seratoS->addVertexShader(vertexShader) &&
      seratoS->addFragmentShader(seratoFragmentShader) && seratoS->link()) {
    seratoShader = std::move(seratoS);
  }

  // Fireball Shader
  std::unique_ptr<juce::OpenGLShaderProgram> fireS(
      new juce::OpenGLShaderProgram(openGLContext));
  if (fireS->addVertexShader(vertexShader) &&
      fireS->addFragmentShader(fireBallFragmentShader) && fireS->link()) {
    fireballShader = std::move(fireS);
  }

  // Cloud Vortex Background Shader (circular view)
  std::unique_ptr<juce::OpenGLShaderProgram> cloudS(
      new juce::OpenGLShaderProgram(openGLContext));
  if (cloudS->addVertexShader(vertexShader) &&
      cloudS->addFragmentShader(cloudVortexFragmentShader) && cloudS->link()) {
    cloudVortexShader = std::move(cloudS);
  }

  // Phase Correlation Shader (uses its own vertex shader)
  std::unique_ptr<juce::OpenGLShaderProgram> phaseS(
      new juce::OpenGLShaderProgram(openGLContext));
  if (phaseS->addVertexShader(PhaseCorrelationShader::vertexShader) &&
      phaseS->addFragmentShader(PhaseCorrelationShader::fragmentShader) &&
      phaseS->link()) {
    phaseShader = std::move(phaseS);
  }

  // Vectorscope Shader (uses its own vertex shader)
  std::unique_ptr<juce::OpenGLShaderProgram> vecS(
      new juce::OpenGLShaderProgram(openGLContext));
  if (vecS->addVertexShader(VectorscopeShader::vertexShader) &&
      vecS->addFragmentShader(VectorscopeShader::fragmentShader) &&
      vecS->link()) {
    vectorscopeShader = std::move(vecS);
  }

  // Vectorscope Glow Shader
  std::unique_ptr<juce::OpenGLShaderProgram> vecGlowS(
      new juce::OpenGLShaderProgram(openGLContext));
  if (vecGlowS->addVertexShader(VectorscopeShader::glowVertexShader) &&
      vecGlowS->addFragmentShader(VectorscopeShader::glowFragmentShader) &&
      vecGlowS->link()) {
    vectorscopeGlowShader = std::move(vecGlowS);
  }

  // Cosmic Flare Shader
  std::unique_ptr<juce::OpenGLShaderProgram> bhsS(
      new juce::OpenGLShaderProgram(openGLContext));
  if (bhsS->addVertexShader(vertexShader) &&
      bhsS->addFragmentShader(cosmicFlareFragmentShader) && bhsS->link()) {
    cosmicFlareShader = std::move(bhsS);
  }

  // Volumetric Explosion Shader
  std::unique_ptr<juce::OpenGLShaderProgram> volExpS(
      new juce::OpenGLShaderProgram(openGLContext));
  if (volExpS->addVertexShader(vertexShader) &&
      volExpS->addFragmentShader(volumetricExplosionFragmentShader) &&
      volExpS->link()) {
    volumetricExplosionShader = std::move(volExpS);
  }

  std::unique_ptr<juce::OpenGLShaderProgram> lfS(
      new juce::OpenGLShaderProgram(openGLContext));
  if (lfS->addVertexShader(vertexShader) &&
      lfS->addFragmentShader(liquidFireFragmentShader) && lfS->link()) {
    liquidFireShader = std::move(lfS);
  }
}
