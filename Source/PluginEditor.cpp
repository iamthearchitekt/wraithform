#include "PluginEditor.h"
#include "CircularOscilloscopeShader.h"
#include "CloudVortexShader.h"
#include "CosmicFlareShader.h"
#include "PhaseCorrelationShader.h"
#include "PluginProcessor.h"
#include "SpectrogramShader.h"
#include "SplashCanvas.h"
#include "VisualizerShader.h"
#include "VolumetricExplosionShader.h"
#include "WaveformShader.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_opengl/juce_opengl.h>

//==============================================================================
void logToDesktop(const juce::String &msg) {
  juce::File f(juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                   .getChildFile("wraith_debug.txt"));
  f.appendText(msg + "\n");
}

void WraithFormAudioProcessorEditor::updateFFTSize(FFTSize newSize) {
  currentFFTSize = newSize;
  int order = (int)newSize;
  int size = 1 << order;

  // Thread safety: we are on message thread here (mouseDown)
  // Rendering is on OpenGL thread.
  // Proper way is to signal OpenGL thread to recreate.
  // For now, recreate directly as OpenGL thread usually checks for null or
  // valid state.
  forwardFFT = std::make_unique<juce::dsp::FFT>(order);
  windowingFunction = std::make_unique<juce::dsp::WindowingFunction<float>>(
      size, juce::dsp::WindowingFunction<float>::blackmanHarris);

  fftBuffer.resize(size * 2, 0.0f);
  fftOutput.resize(size / 2, 0.0f);
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
  openGLContext.setContinuousRepainting(true); // Attempt 60FPS or generic VSync

  // Audio Buffers setup
  int initialSize = 1 << (int)FFTSize::AllRound;
  textureData.resize(textureSize, 0.0f);
  phaseData.resize(textureSize, {0.0f, 0.0f}); // Init phase data
  fftBuffer.resize(initialSize * 2, 0.0f);
  fftOutput.resize(initialSize / 2, 0.0f);

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

  logToDesktop("Constructor: Splash Valid? " +
               juce::String(splashImage.isValid() ? "YES" : "NO"));
  if (splashImage.isValid()) {
    logToDesktop("Splash Dims: " + juce::String(splashImage.getWidth()) + "x" +
                 juce::String(splashImage.getHeight()));
  }

  // Fallback: If Base64 fails (unlikely), we just have no splash.
  // logToDesktop("Constructor: Splash Valid? " +
  // juce::String(splashImage.isValid() ? "YES" : "NO"));
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
    }
  } else if (e.mods.isMiddleButtonDown()) {
    isFrozen = !isFrozen;
  } else {
    // RESET Button Hitbox (Bottom Left)
    if (e.x > 20 && e.x < 70 && e.y > getHeight() - 40) {
      audioProcessor.resetLoudnessStats();
      return;
    }

    // QUAD View Toggle (Beside Reset)
    if (e.x > 80 && e.x < 140 && e.y > getHeight() - 40) {
      currentMode = (currentMode == VisualizerMode::QuadView)
                        ? VisualizerMode::Oscilloscope
                        : VisualizerMode::QuadView;
      openGLContext.triggerRepaint();
      return;
    }

    // ZOOM Toggle (Beside QUAD/Color in Linear/Quad Mode)
    if (!zoomBtnRect.isEmpty() && zoomBtnRect.contains(e.getPosition())) {
      if (waveformZoom < 2.0f)
        waveformZoom = 2.0f;
      else if (waveformZoom < 4.0f)
        waveformZoom = 4.0f;
      else
        waveformZoom = 1.0f;

      openGLContext.triggerRepaint();
      return;
    }

    // COLOR MODE Toggle (Beside QUAD)
    if (e.x > 148 && e.x < 218 && e.y > getHeight() - 40) {
      if (currentColorMode == ColorMode::Default)
        currentColorMode = ColorMode::UV;
      else if (currentColorMode == ColorMode::UV)
        currentColorMode = ColorMode::Infrared;
      else if (currentColorMode == ColorMode::Infrared)
        currentColorMode = ColorMode::Heat;
      else if (currentColorMode == ColorMode::Heat)
        currentColorMode = ColorMode::Plasma;
      else
        currentColorMode = ColorMode::Default;
      openGLContext.triggerRepaint();
      return;
    }

    // MULTI Window Toggle (Beside Quad) - DISABLED
    if (e.x > 140 && e.x < 190 && e.y > getHeight() - 40) {
      // toggleDetachedWindows();
      return;
    }

    // FULL SCREEN Toggle REMOVED

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
      currentMode = VisualizerMode::CosmicFlare;
    else if (currentMode == VisualizerMode::CosmicFlare)
      currentMode = VisualizerMode::VolumetricExplosion;
    else if (currentMode == VisualizerMode::VolumetricExplosion)
      currentMode = VisualizerMode::QuadView;
    else if (currentMode == VisualizerMode::QuadView)
      currentMode = VisualizerMode::Oscilloscope;

    createShaders();
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
  // Search in the first half of the search buffer
  float threshold = 0.505f; // Since zero is 0.5 in encoded visualizer buffer
  for (int i = 1; i < size / 2; ++i) {
    if (data[i] > threshold && data[i - 1] <= threshold) {
      return i;
    }
  }
  return 0;
}

// Returns the active skin colour as a JUCE Colour
juce::Colour WraithFormAudioProcessorEditor::getThemeColour() const {
  if (currentColorMode == ColorMode::UV)
    return juce::Colour(0xFF6103FF); // Deep blue-violet (UV)
  else if (currentColorMode == ColorMode::Infrared)
    return juce::Colour(0xFFFF3A28); // Deep Red
  else if (currentColorMode == ColorMode::Heat)
    return juce::Colour(0xFFFF7200); // Flame Orange
  else if (currentColorMode == ColorMode::Plasma)
    return juce::Colour(0xFF33FF11); // Radioactive Green
  return juce::Colour(0xFFD5FFFF);   // Default Ice Blue
}

// RGB triplet for OpenGL (passed to shader or glClearColor)
void WraithFormAudioProcessorEditor::getThemeRGB(float &r, float &g,
                                                 float &b) const {
  if (currentColorMode == ColorMode::UV) {
    r = 0.38f;
    g = 0.02f;
    b = 1.0f;
  } else if (currentColorMode == ColorMode::Infrared) {
    r = 1.0f;
    g = 0.23f;
    b = 0.16f;
  } else if (currentColorMode == ColorMode::Heat) {
    r = 1.0f;
    g = 0.45f;
    b = 0.05f;
  } else if (currentColorMode == ColorMode::Plasma) {
    r = 0.2f;
    g = 1.0f;
    b = 0.07f;
  } // Radioactive Green
  else {
    r = 0.835f;
    g = 1.0f;
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
  glClearColor(0.0f, 0.0f, 0.0f, 0.45f); // Transparent Sidebar
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
  int dashH = (int)(55 * scale);

  // Background Bar (Top)
  glEnable(GL_SCISSOR_TEST);
  glScissor(0, (int)((h - 55) * scale), (int)(w * scale), (int)(55 * scale));
  glClearColor(0.0f, 0.0f, 0.0f, 0.45f); // Transparent Dashboard
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);

  std::unique_ptr<juce::LowLevelGraphicsContext> glContext(
      juce::createOpenGLGraphicsContext(openGLContext, (int)(w * scale),
                                        (int)(h * scale)));
  if (glContext != nullptr) {
    juce::Graphics g(*glContext);
    g.addTransform(juce::AffineTransform::scale(scale));

    auto drawMetric = [&](int x, int y, const char *label, float value) {
      juce::String text = (value <= -90.0f) ? "-inf" : juce::String(value, 1);
      juce::Colour tc = getThemeColour();
      g.setColour(tc.withAlpha(0.6f));
      g.setFont(9.0f);
      g.drawText(label, x, y - 14, 120, 10, juce::Justification::left);
      g.setColour(tc);
      g.setFont(16.0f);
      g.drawText(text, x, y, 120, 20, juce::Justification::left);
      g.setFont(9.0f);
      g.setColour(tc.withAlpha(0.4f));
      g.drawText("LUFS", x + 48, y + 4, 30, 10, juce::Justification::left);
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
    g.drawText("QUAD", quadBtnRect, juce::Justification::centred);

    // COLOR MODE Button (Beside QUAD)
    juce::Rectangle<int> colorBtnRect = {150, footerY, 65, 20};

    // Fill background solid black
    g.setColour(juce::Colours::black);
    g.fillRect(colorBtnRect);

    // Pick swatch colour per mode
    juce::Colour swatchColor;
    juce::String colorLabel;
    if (currentColorMode == ColorMode::Default) {
      swatchColor = juce::Colour(0xFFD5FFFF);
      colorLabel = "DEFAULT";
    } else if (currentColorMode == ColorMode::UV) {
      swatchColor = juce::Colour(0xFF6103FF);
      colorLabel = "UV";
    } else if (currentColorMode == ColorMode::Infrared) {
      swatchColor = juce::Colour(0xFFFF3A28);
      colorLabel = "INFRA";
    } else if (currentColorMode == ColorMode::Plasma) {
      swatchColor = juce::Colour(0xFF33FF11);
      colorLabel = "PLASMA";
    } else {
      swatchColor = juce::Colour(0xFFFF7200);
      colorLabel = "HEAT";
    }
    g.setColour(swatchColor.withAlpha(0.85f));
    g.drawRect(colorBtnRect, 1);
    g.setFont(10.0f);
    g.drawText(colorLabel, colorBtnRect, juce::Justification::centred);

    // ZOOM Button (Visible in Oscilloscope, Serato and QuadView)
    if (currentMode == VisualizerMode::Oscilloscope ||
        currentMode == VisualizerMode::Serato ||
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

  jassert(OpenGLHelpers::isContextActive());

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

    // Cosmic Flare Speed: also reactive to bass energy
    float flareSpeed = 0.2f + 2.8f * std::pow(smoothedBassEnergy, 2.0f);
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
    // Smooth it gently so visuals don't snap
    smoothedBassEnergy = smoothedBassEnergy * 0.88f + bassEnergy * 0.12f;

    // Kick energy for more peaky effects (fireball)
    float kickEnergy = 0.0f;
    int kickBins = std::min(6, (int)fftOutput.size());
    for (int i = 1; i < kickBins; ++i)
      kickEnergy = std::max(kickEnergy, fftOutput[i]);
    smoothedKickEnergy = smoothedKickEnergy * 0.8f + kickEnergy * 0.2f;
  }

  // SIDEBAR ANIMATION (Self-driven without Timer)
  if (isSideBarVisible && sideBarAnimation < 1.0f)
    sideBarAnimation = std::min(1.0f, sideBarAnimation + 0.05f);
  else if (!isSideBarVisible && sideBarAnimation > 0.0f)
    sideBarAnimation = std::max(0.0f, sideBarAnimation - 0.05f);

  // Clear background to Black (CRITICAL FOR STARTUP GLITCH FIX)
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // Enable Alpha Blending
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // FORCE RESET VIEWPORT/SCISSOR to full window
  // This ensures no leftover state from QuadView or other overlays affects the
  // main render
  float desktopScale = (float)openGLContext.getRenderingScale();
  glViewport(0, 0, (int)(getWidth() * desktopScale),
             (int)(getHeight() * desktopScale));
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE); // PREVENT DIAGONAL LINE ARTIFACTS

  // 1. Render Active Mode
  if (currentMode == VisualizerMode::Spectrogram)
    renderSpectrogram();
  else if (currentMode == VisualizerMode::Circular)
    renderCircularOscilloscope();
  else if (currentMode == VisualizerMode::Serato)
    renderSeratoWaveform();
  else if (currentMode == VisualizerMode::PhaseMeter)
    renderPhaseMeter(); // Added Phase Meter case
  else if (currentMode == VisualizerMode::CosmicFlare)
    renderCosmicFlare();
  else if (currentMode == VisualizerMode::VolumetricExplosion)
    renderVolumetricExplosion();
  else if (currentMode == VisualizerMode::QuadView)
    renderQuadView();
  else
    renderOscilloscope();

  // 2. Overlay Splash Transition - REMOVED PER USER REQUEST
  // if (isTransitioning) { ... }

  // 3. Draw Professional Loudness Dashboard (Top Overlay)
  renderLoudnessDashboard();

  // 4. Draw Meters Sidebar
  renderMeters();

  // 5. Draw Phase Correlation Meter Overlay
  // A small bar at the bottom: -1 (Left/Red) to +1 (Right/Cyan)
  smoothedCorrelation =
      smoothedCorrelation * 0.95f + currentCorrelation * 0.05f;

  if (currentMode == VisualizerMode::Serato ||
      currentMode == VisualizerMode::Spectrogram) {
    glEnable(GL_SCISSOR_TEST);
    int w = getWidth();
    int h = getHeight();
    float scale = (float)openGLContext.getRenderingScale();

    // Background bar
    glScissor((int)(10 * scale), (int)(10 * scale), (int)((w - 20) * scale),
              (int)(5 * scale));
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Pure Black Phase Background
    glClear(GL_COLOR_BUFFER_BIT);

    // Meter Fill
    float x_norm = (smoothedCorrelation + 1.0f) * 0.5f; // 0..1
    int meterWidth = (int)((w - 20) * scale);
    glScissor((int)(10 * scale), (int)(10 * scale), (int)(x_norm * meterWidth),
              (int)(5 * scale));

    if (smoothedCorrelation < 0)
      glClearColor(0.4f, 0.45f, 0.5f, 0.8f); // Slate Grey
    else
      glClearColor(0.5f, 0.65f, 0.75f, 0.8f); // Steel Cyan

    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
  }
}

void WraithFormAudioProcessorEditor::renderCircularOscilloscope() {
  using namespace juce::gl;
  auto desktopScale = (float)openGLContext.getRenderingScale();

  if (circularShader == nullptr)
    return;

  // --- 1. Render CloudVortex background (opaque) ---
  if (cloudVortexShader != nullptr) {
    cloudVortexShader->use();
    GLint vp2[4];
    glGetIntegerv(GL_VIEWPORT, vp2);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform2f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_resolution"),
        (GLfloat)vp2[2], (GLfloat)vp2[3]);

    // Map time uniforms correctly:
    // u_time drives the slow hue/sat cycle (prm1)
    // u_cloudT drives the integrated speed tunnel motion (T)
    // Map time uniforms correctly:
    // u_time drives the slow hue/sat cycle (prm1) - use stableTime for
    // smoothness u_cloudT drives the integrated speed tunnel motion (T)
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

  // --- 2. Render circular oscilloscope ring ---
  circularShader->use();

  // Update Uniforms
  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);

  GLint resolutionLoc =
      glGetUniformLocation(circularShader->getProgramID(), "u_resolution");
  if (resolutionLoc > -1)
    glUniform2f(resolutionLoc, (GLfloat)vp[2], (GLfloat)vp[3]);

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

  // --- Render Fireball Core (normal alpha blend = truly on top of ring) ---
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  if (fireballShader != nullptr) {
    fireballShader->use();
    glUniform2f(
        glGetUniformLocation(fireballShader->getProgramID(), "u_resolution"),
        (GLfloat)vp[2], (GLfloat)vp[3]);
    glUniform1f(glGetUniformLocation(fireballShader->getProgramID(), "u_time"),
                (GLfloat)cosmicFlareTime);

    // kickEnergy: bins 1-5 (~21-107Hz) with peak-hold decay
    // — punchy transients: kick drum hits register immediately and decay slowly
    float rawKick = 0.0f;
    int kickBins = std::min(6, (int)fftOutput.size());
    for (int i = 1; i < kickBins; ++i)
      rawKick += fftOutput[i];
    rawKick = juce::jlimit(0.0f, 1.0f, rawKick / (float)(kickBins - 1));
    // Peak-hold: rise fast, decay slowly (~3-4 frames at 60fps)
    smoothedKickEnergy = std::max(smoothedKickEnergy * 0.82f, rawKick);

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
  // 1. Pull latest audio for Oscilloscope (using Mono sum for display)
  // We use a larger window to find a trigger point for stabilization
  const int searchSize = textureSize * 2;
  std::vector<float> wideL(searchSize), wideR(searchSize);
  audioProcessor.visualizerBufferL.readHistory(wideL, searchSize);
  audioProcessor.visualizerBufferR.readHistory(wideR, searchSize);

  std::vector<float> wideSum(searchSize);
  for (int i = 0; i < searchSize; ++i)
    wideSum[i] = (wideL[i] + wideR[i]) * 0.5f;

  int trigger = findTriggerPoint(wideSum, searchSize);

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
    float v1 = wideSum[trigger + idx1];
    float v2 = wideSum[trigger + idx2];
    textureData[i] = v1 + (v2 - v1) * frac;
  }

  // 1. Upload Image to Texture
  // ... (existing code) ...

  // 1.5 Populate Phase Data (Stereo)
  // We need synchronized L/R samples for the goniometer.
  // We use a smaller window than the search, just enough for the frame.
  int phasePoints = 2048; // Number of points to draw (Increased for detail)
  if (phaseData.size() != phasePoints)
    phaseData.resize(phasePoints);

  std::vector<float> phaseL(phasePoints), phaseR(phasePoints);
  audioProcessor.visualizerBufferL.readHistory(phaseL, phasePoints);
  audioProcessor.visualizerBufferR.readHistory(phaseR, phasePoints);

  for (int i = 0; i < phasePoints; ++i) {
    phaseData[i] = juce::Point<float>(phaseL[i], phaseR[i]);
  }

  // 2. FFT uses Mono sum
  // Note: For a real-to-frequency FFT of size N, we provide N samples.
  // The buffer must be size 2N to accommodate complex output.
  // The buffer must be size 2N to accommodate complex output.
  int size = (int)1 << (int)currentFFTSize;
  std::vector<float> fftInL(size), fftInR(size);
  audioProcessor.visualizerBufferL.readHistory(fftInL, size);
  audioProcessor.visualizerBufferR.readHistory(fftInR, size);

  // 2B. Ensure Buffers are Large Enough (CRITICAL CRASH FIX)
  // If currentFFTSize changes, we MUST resize vectors to avoid segfault.
  if (fftBuffer.size() < size) {
    fftBuffer.resize(size, 0.0f);
  }
  if (fftOutput.size() < size / 2) {
    fftOutput.resize(size / 2, 0.0f);
  }

  for (int i = 0; i < size; ++i) {
    if (i < fftInL.size() && i < fftInR.size()) // Extra safety
      fftBuffer[i] = (fftInL[i] + fftInR[i]) * 0.5f;
  }

  windowingFunction->multiplyWithWindowingTable(fftBuffer.data(), size);
  forwardFFT->performFrequencyOnlyForwardTransform(fftBuffer.data());

  // 3. Map to Output (0..1)
  int outputSize = size / 2; // Use theoretical size, not buffer capacity
  for (int i = 0; i < outputSize; ++i) {
    if (i >= fftOutput.size())
      break; // Safety break

    float mag = fftBuffer[i];
    float db = juce::Decibels::gainToDecibels(mag) -
               juce::Decibels::gainToDecibels((float)(outputSize * 2));
    float norm = juce::jmap(db, -90.0f, 0.0f, 0.0f, 1.0f);
    fftOutput[i] = juce::jlimit(0.0f, 1.0f, norm);
  }

  // 4. Update Spectrogram Image (CPU Shift)
  if (spectrogramImage.isValid()) {
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
      // Map y (0..h) to FFT bin (0..fftSize/2)
      // Logarithmic-ish mapping for better visuals
      float normalizedY =
          1.0f -
          (float)y / (float)h; // 1.0 at top, 0.0 at bottom (low freq bottom)
      // normalizedY = normalizedY * normalizedY; // simple exp curve

      int bin = (int)(normalizedY * outputSize);
      if (bin >= outputSize)
        bin = outputSize - 1;
      if (bin < 0)
        bin = 0;

      float val = (bin < fftOutput.size()) ? fftOutput[bin] : 0.0f;

      // PUNCHY DYNAMIC RANGE: Apply subtle gamma for more mid-level detail
      val = pow(val, 0.85f);

      // Color Map (Strict Ice Blue Brand Compliance + High Brightness)
      juce::Colour col = juce::Colour(0xFF000000); // Black
      if (val > 0.01f) {
        if (val < 0.4f) {
          // Black -> Dark Deep Cyan (Faster ramp)
          col = juce::Colour(0xFF000000)
                    .interpolatedWith(juce::Colour(0xFF005566), val * 2.5f);
        } else {
          // Dark Deep Cyan -> Radiant Ice Blue (#D5FFFF)
          col = juce::Colour(0xFF005566)
                    .interpolatedWith(juce::Colour(0xFFD5FFFF),
                                      (val - 0.4f) * 1.66f);
        }
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
  glUniform1f(glGetUniformLocation(seratoShader->getProgramID(), "u_zoom"),
              waveformZoom);

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

  int halfH = (int)((h * desktopScale) / 2);
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

  // --- 1. Render CloudVortex background ---
  if (cloudVortexShader != nullptr) {
    glDisable(GL_CULL_FACE); // Extra safety for the background quad

    cloudVortexShader->use();
    glUniform1f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_time"),
        (GLfloat)juce::Time::getMillisecondCounter() / 1000.0f);
    glUniform2f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_resolution"),
        (GLfloat)getWidth(), (GLfloat)getHeight());
    glUniform1f(
        glGetUniformLocation(cloudVortexShader->getProgramID(), "u_cloudT"),
        cloudTunnelTime);

    // Pass audio energy
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

    GLint posAttr =
        glGetAttribLocation(cloudVortexShader->getProgramID(), "position");
    glEnableVertexAttribArray(posAttr);

    // Static quad for zero-jitter and correct winding
    static const GLfloat v[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                -1.0f, 1.0f,  1.0f, 1.0f};
    glVertexAttribPointer(posAttr, 2, GL_FLOAT, GL_FALSE, 0, v);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(posAttr);
  }

  // --- 2. Render Phase Meter Particles ---
  phaseShader->use();

  // Robust State Setup
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Pure Additive for strong glow
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glPointSize(2.0f); // Decreased for finer detail

  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);

  // Uniforms
  glUniform2f(glGetUniformLocation(phaseShader->getProgramID(), "u_resolution"),
              (GLfloat)vp[2], (GLfloat)vp[3]);
  glUniform1f(glGetUniformLocation(phaseShader->getProgramID(), "scale"),
              2.45f);
  {
    float tr, tg, tb;
    getThemeRGB(tr, tg, tb);
    glUniform4f(glGetUniformLocation(phaseShader->getProgramID(), "color"), tr,
                tg, tb, 0.9f);
  }

  // Data Selection
  const juce::Point<float> *dataPtr = nullptr;
  int dataSize = 0;

  // Safe Mode Ring
  static std::vector<juce::Point<float>> testPoints;
  if (testPoints.empty()) {
    for (float f = 0.0f; f < 6.28f; f += 0.1f) {
      float rad = 0.5f;
      testPoints.push_back({sin(f) * rad, cos(f) * rad}); // Ring
    }
    testPoints.push_back({0.0f, 0.0f}); // Center Dot
  }

  if (phaseData.size() > 0) {
    dataPtr = phaseData.data();
    dataSize = (int)phaseData.size();
  } else {
    dataPtr = testPoints.data();
    dataSize = (int)testPoints.size();
  }

  // Draw Points
  glPointSize(4.0f);
  GLint posLoc = glGetAttribLocation(phaseShader->getProgramID(), "position");
  if (posLoc > -1 && dataSize > 0 && dataPtr != nullptr) {
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 0, dataPtr);
    glDrawArrays(GL_POINTS, 0, dataSize);
    glDisableVertexAttribArray(posLoc);
  }
}

void WraithFormAudioProcessorEditor::renderCosmicFlare() {
  using namespace juce::gl;
  if (cosmicFlareShader == nullptr)
    return;

  cosmicFlareShader->use();

  cosmicFlareShader->setUniform("u_time", cosmicFlareTime);
  cosmicFlareShader->setUniform("u_resolution", (float)getWidth(),
                                (float)getHeight());
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
  if (volumetricExplosionShader == nullptr)
    return;

  volumetricExplosionShader->use();

  volumetricExplosionShader->setUniform("u_time", (float)cosmicFlareTime);
  volumetricExplosionShader->setUniform("u_resolution", (float)getWidth(),
                                        (float)getHeight());
  volumetricExplosionShader->setUniform("u_audioEnergy", smoothedBassEnergy);
  volumetricExplosionShader->setUniform("u_kickEnergy", smoothedKickEnergy);

  auto mousePos = getMouseXYRelative();
  volumetricExplosionShader->setUniform("u_mouse",
                                        (float)mousePos.x / (float)getWidth(),
                                        (float)mousePos.y / (float)getHeight());

  float tr, tg, tb;
  getThemeRGB(tr, tg, tb);
  volumetricExplosionShader->setUniform("u_glowColor", tr, tg, tb);

  // Setup Quad Geometry for Full Screen Shader
  static const GLfloat qVerts[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                   -1.0f, 1.0f,  1.0f, 1.0f};

  GLint posAttrib = openGLContext.extensions.glGetAttribLocation(
      volumetricExplosionShader->getProgramID(), "position");
  if (posAttrib > -1) {
    openGLContext.extensions.glEnableVertexAttribArray(posAttrib);
    openGLContext.extensions.glVertexAttribPointer(posAttrib, 2, GL_FLOAT,
                                                   GL_FALSE, 0, qVerts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    openGLContext.extensions.glDisableVertexAttribArray(posAttrib);
  }
}

void WraithFormAudioProcessorEditor::createShaders() {
  // Minimal Vertex Shader with UV pass-through
  juce::String vertexShader = R"(
        attribute vec2 position;
        varying vec2 v_uv;
        void main()
        {
            gl_Position = vec4(position, 0.0, 1.0);
            v_uv = position * 0.5 + 0.5;
        }
    )";

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
    varying vec2 v_uv;
    uniform sampler2D u_image;
    uniform float u_alpha;
    void main()
    {
        vec4 color = texture2D(u_image, v_uv);
        gl_FragColor = vec4(color.rgb, color.a * u_alpha);
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
}
