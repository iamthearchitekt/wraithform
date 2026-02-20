#include "PluginEditor.h"
#include "CircularOscilloscopeShader.h"
#include "PhaseCorrelationShader.h"
#include "PluginProcessor.h"
#include "SpectrogramShader.h"
#include "SplashCanvas.h"
#include "VisualizerShader.h"
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
      juce::dsp::WindowingFunction<float>::blackmanHarris);

  using namespace juce::gl;
  // Make sure that before the constructor has finished, you've set the
  // editor's size to whatever you need it to be.
  setSize(1280, 720);
  setResizable(true, true);

  // Setup OpenGL
  openGLContext.setRenderer(this);
  openGLContext.attachTo(*this);
  openGLContext.setContinuousRepainting(true); // Attempt 60FPS or generic VSync

  // Setup Update Button
  addAndMakeVisible(updateButton);
  updateButton.setButtonText("Update Available!");
  updateButton.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(0xFF4CAF50)); // Nice green
  updateButton.setColour(juce::TextButton::textColourOffId,
                         juce::Colours::white);
  updateButton.setVisible(false);
  updateButton.onClick = [this] {
    auto info = audioProcessor.getLatestUpdateInfo();
    if (info.downloadUrl.isNotEmpty())
      juce::URL(info.downloadUrl).launchInDefaultBrowser();
  };

  // Start checking for updates every 10 seconds (after initial delay)
  startTimer(10000);

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

void WraithFormAudioProcessorEditor::timerCallback() {
  auto info = audioProcessor.getLatestUpdateInfo();

  if (info.updateAvailable && !showUpdateNotification) {
    showUpdateNotification = true;
    updateButton.setVisible(true);
    resized(); // Re-layout to show button
  }
}

WraithFormAudioProcessorEditor::~WraithFormAudioProcessorEditor() {}

//==============================================================================
//==============================================================================
//==============================================================================
void WraithFormAudioProcessorEditor::paint(juce::Graphics &g) {
  // Allow OpenGL to clear the screen
}

void WraithFormAudioProcessorEditor::resized() {
  auto area = getLocalBounds();

  // Position Update Button at the top, centered
  if (showUpdateNotification) {
    updateBtnRect = area.removeFromTop(40).withSizeKeepingCentre(200, 30);
    updateButton.setBounds(updateBtnRect);
  }
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
    if (e.x > 80 && e.x < 130 && e.y > getHeight() - 40) {
      currentMode = (currentMode == VisualizerMode::QuadView)
                        ? VisualizerMode::Oscilloscope
                        : VisualizerMode::QuadView;
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
    return;
    if (phaseShader == nullptr) {
      phaseShader.reset(new juce::OpenGLShaderProgram(openGLContext));
      if (phaseShader->addVertexShader(PhaseCorrelationShader::vertexShader) &&
          phaseShader->addFragmentShader(
              PhaseCorrelationShader::fragmentShader) &&
          phaseShader->link()) {
        // Linked
      } else {
        logToDesktop("Phase Shader Error: " + phaseShader->getLastError());
      }
    }
  }

  // Create windows for each mode
  detachedWindows.push_back(std::make_unique<DetachedWindow>(
      "Oscilloscope", *this, VisualizerMode::Oscilloscope));
  numDetachedWindows++;
  detachedWindows.push_back(std::make_unique<DetachedWindow>(
      "Circular", *this, VisualizerMode::Circular));
  numDetachedWindows++;
  detachedWindows.push_back(std::make_unique<DetachedWindow>(
      "Serato", *this, VisualizerMode::Serato));
  numDetachedWindows++;
  detachedWindows.push_back(std::make_unique<DetachedWindow>(
      "Spectrogram", *this, VisualizerMode::Spectrogram));
  numDetachedWindows++;
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
  glClearColor(0.01f, 0.01f, 0.05f, 0.8f);
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
  int meterHeight =
      (int)(h - 140 * scale); // Reduced height to fit raised start

  auto drawMeter = [&](int x_off, float val, bool isRMS) {
    int barH = (int)(val * meterHeight);
    // Raise base to 110 to clear text at bottom (text occupies up to ~90 from
    // bottom)
    glScissor(x_start + x_off, (int)(110 * scale), barW, barH);
    if (isRMS)
      glClearColor(0.2f, 0.35f, 0.5f, 0.9f); // Desaturated Steel Blue
    else
      glClearColor(0.4f, 0.6f, 0.7f, 1.0f); // Desaturated Pale Cyan
    glClear(GL_COLOR_BUFFER_BIT);
  };

  drawMeter(barGap, normPeakL, false);
  drawMeter(barGap + barW + 2, normPeakR, false);
  drawMeter(barGap * 3, normRMSL, true);
  drawMeter(barGap * 3 + barW + 2, normRMSR, true);

  // LUFS Bar
  int lufs_off = barGap * 5;
  int lufs_H = (int)(normLUFS * meterHeight);
  glScissor(x_start + lufs_off, (int)(110 * scale), barW * 2, lufs_H);
  glClearColor(1.0f, 1.0f, 1.0f, 0.8f); // White for LUFS
  glClear(GL_COLOR_BUFFER_BIT);

  glDisable(GL_SCISSOR_TEST);

  // --- Dynamic Text Overlay (Numeric Readouts & Labels) ---
  std::unique_ptr<juce::LowLevelGraphicsContext> glContext(
      juce::createOpenGLGraphicsContext(openGLContext, getWidth(),
                                        getHeight()));
  if (glContext != nullptr) {
    juce::Graphics g(*glContext);
    g.addTransform(juce::AffineTransform::scale(scale));

    float fontSize = 10.0f;
    g.setFont(fontSize);
    g.setColour(juce::Colours::white.withAlpha(0.8f));

    auto drawReadout = [&](int x, int y, float dbValue, const char *label) {
      juce::String text =
          (dbValue <= -99.0f) ? "-inf" : juce::String(dbValue, 1);
      int x_pos = (int)(x / scale);
      int y_pos = (int)(y / scale);

      g.setColour(juce::Colours::cyan.withAlpha(0.6f));
      g.drawText(label, x_pos, y_pos - 12, 40, 10, juce::Justification::left);

      g.setColour(juce::Colours::white);
      g.drawText(text, x_pos, y_pos, 40, 12, juce::Justification::left);
    };

    int textX = (int)(x_start / scale) + 5;
    int textY_Bottom = (int)(h / scale) - 35;
    int textY_Top = 25;

    // L/R Labels at top
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawText("L", (int)(x_start / scale) + 10, textY_Top, 10, 10,
               juce::Justification::centred);
    g.drawText("R", (int)(x_start / scale) + 20, textY_Top, 10, 10,
               juce::Justification::centred);

    // Move readouts to the bottom area (below 40px meter start)
    float dbPeakL = juce::Decibels::gainToDecibels(pL);
    float dbPeakR = juce::Decibels::gainToDecibels(pR);
    float dbRMSL = juce::Decibels::gainToDecibels(rL);
    float dbRMSR = juce::Decibels::gainToDecibels(rR);

    // Sidebar footer readouts
    // Increased vertical spacing to prevent overlap
    // Row 1: Peak & RMS
    int row1_Y = (int)(h - 75 * scale); // Moving UP (was 55)
    drawReadout((int)(x_start + 10 * scale), row1_Y, std::max(dbPeakL, dbPeakR),
                "PEAK");
    drawReadout((int)(x_start + 55 * scale), row1_Y, (dbRMSL + dbRMSR) * 0.5f,
                "RMS");

    // Row 2: LUFS (Moved up)
    int row2_Y = (int)(h - 35 * scale); // Moving UP (was 15)
    drawReadout((int)(x_start + 10 * scale), row2_Y, lufs, "LUFS");
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
  glScissor(0, (int)((h * scale) - dashH), (int)(w * scale), dashH);
  glClearColor(0.01f, 0.01f, 0.05f, 0.95f); // Deep dark
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);

  // Text Overlay
  std::unique_ptr<juce::LowLevelGraphicsContext> glContext(
      juce::createOpenGLGraphicsContext(openGLContext, w, h));
  if (glContext != nullptr) {
    juce::Graphics g(*glContext);
    g.addTransform(juce::AffineTransform::scale(scale));

    auto drawMetric = [&](int x, int y, const char *label, float value,
                          bool isLarge) {
      juce::String text = (value <= -90.0f) ? "-inf" : juce::String(value, 1);

      g.setColour(juce::Colour(0xFFD5FFFF).withAlpha(0.6f));
      g.setFont(9.0f);
      g.drawText(label, x, y - 15, 80, 12, juce::Justification::left);

      g.setColour(juce::Colour(0xFFD5FFFF));
      g.setFont(juce::Font(isLarge ? 22.0f : 14.0f, juce::Font::bold));
      g.drawText(text, x, y, 85, isLarge ? 24 : 16, juce::Justification::left);

      g.setFont(9.0f);
      g.setColour(juce::Colour(0xFFD5FFFF).withAlpha(0.4f));
      // Increased offset to 65 for large text so it doesn't overlap
      g.drawText("LUFS", x + (isLarge ? 65 : 45), y + (isLarge ? 8 : 4), 30, 10,
                 juce::Justification::left);
    };

    int startX = 20;
    int yOff = 22;
    int spacing = 150; // Increased from ~100 to 150 to prevent overlap

    drawMetric(startX, yOff, "INTEGRATED", audioProcessor.lufsIntegrated.load(),
               true);
    drawMetric(startX + spacing, yOff, "SHORT-TERM",
               audioProcessor.lufsShortTerm.load(), false);
    drawMetric(startX + spacing * 2, yOff, "MOMENTARY",
               audioProcessor.lufsMomentary.load(), false);

    // True Peak Readout (Relocated left to avoid sidebar overlap)
    int tpX = w - 260; // Shifted slightly more left
    g.setColour(juce::Colour(0xFFD5FFFF).withAlpha(0.6f));
    g.setFont(9.0f);
    g.drawText("TRUE PEAK (L/R)", tpX, yOff - 15, 120, 12,
               juce::Justification::left);

    g.setColour(juce::Colour(0xFFD5FFFF));
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    juce::String tpText = juce::String(audioProcessor.truePeakL.load(), 1) +
                          " / " +
                          juce::String(audioProcessor.truePeakR.load(), 1);
    g.drawText(tpText, tpX, yOff, 120, 16, juce::Justification::left);

    // FULL SCREEN Button REMOVED

    // Reset Button (Themed, Bottom Left)
    int footerY = h - 32;
    g.setColour(juce::Colour(0xFFD5FFFF).withAlpha(0.6f));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawRect(20, footerY, 50, 20, 1);
    g.drawText("RESET", 20, footerY, 50, 20, juce::Justification::centred);

    // Quad/Multi Buttons (Beside Reset)
    quadBtnRect = {80, footerY, 50, 20};
    multiBtnRect = {140, footerY, 50, 20};

    g.setColour(currentMode == VisualizerMode::QuadView
                    ? juce::Colour(0xFFD5FFFF)
                    : juce::Colour(0xFFD5FFFF).withAlpha(0.6f));
    g.drawRect(quadBtnRect, 1);
    g.setFont(9.0f);
    g.drawText("MULTI", quadBtnRect, juce::Justification::centred);

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

  // 1. Render Active Mode
  if (currentMode == VisualizerMode::Spectrogram)
    renderSpectrogram();
  else if (currentMode == VisualizerMode::Circular)
    renderCircularOscilloscope();
  else if (currentMode == VisualizerMode::Serato)
    renderSeratoWaveform();
  else if (currentMode == VisualizerMode::PhaseMeter)
    renderPhaseMeter(); // Added Phase Meter case
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
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
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
    glUniform1f(timeLoc,
                (GLfloat)juce::Time::getMillisecondCounter() / 1000.0f);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, audioTextureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, textureSize, 1, 0, GL_RED, GL_FLOAT,
               textureData.data());

  GLint audioLoc =
      glGetUniformLocation(circularShader->getProgramID(), "u_audioData");
  if (audioLoc > -1)
    glUniform1i(audioLoc, 0); // slot 0

  // Draw Full Screen Quad
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

  // --- Render Fireball Core ---
  if (fireballShader != nullptr) {
    fireballShader->use();
    glUniform2f(
        glGetUniformLocation(fireballShader->getProgramID(), "u_resolution"),
        (GLfloat)vp[2], (GLfloat)vp[3]);
    glUniform1f(glGetUniformLocation(fireballShader->getProgramID(), "u_time"),
                (GLfloat)juce::Time::getMillisecondCounter() / 1000.0f);

    // Draw a smaller quad in the center for the fireball
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

  for (int i = 0; i < textureSize; ++i) {
    textureData[i] = wideSum[trigger + i];
  }

  // 1. Upload Image to Texture
  // ... (existing code) ...

  // 1.5 Populate Phase Data (Stereo)
  // We need synchronized L/R samples for the goniometer.
  // We use a smaller window than the search, just enough for the frame.
  int phasePoints = 1024; // Number of points to draw
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

      // Color Map (Ice Blue)
      // Color Map (Strict Ice Blue Brand Compliance)
      juce::Colour col = juce::Colour(0xFF000000); // Black
      if (val > 0.0f) {
        if (val < 0.5f) {
          // Black -> Dark Deep Cyan
          col = juce::Colour(0xFF000000)
                    .interpolatedWith(juce::Colour(0xFF004050), val * 2.0f);
        } else {
          // Dark Deep Cyan -> Ice Blue (#D5FFFF)
          col = juce::Colour(0xFF004050)
                    .interpolatedWith(juce::Colour(0xFFD5FFFF),
                                      (val - 0.5f) * 2.0f);
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
              (float)juce::Time::getMillisecondCounter() / 1000.0f);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, waveformHistoryTexture);
  glUniform1i(glGetUniformLocation(seratoShader->getProgramID(), "u_history"),
              0);

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

  // CRITICAL FIX: Must activate the shader!
  phaseShader->use();

  // Robust State Setup
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glPointSize(4.0f);

  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);

  // Uniforms
  glUniform2f(glGetUniformLocation(phaseShader->getProgramID(), "u_resolution"),
              (GLfloat)vp[2], (GLfloat)vp[3]);
  glUniform1f(glGetUniformLocation(phaseShader->getProgramID(), "scale"),
              2.45f); // Reduced by 30% from 3.5
  glUniform4f(glGetUniformLocation(phaseShader->getProgramID(), "color"),
              0.835f, 1.0f, 1.0f, 0.9f);

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

  // Phase Correlation Shader
  std::unique_ptr<juce::OpenGLShaderProgram> phaseS(
      new juce::OpenGLShaderProgram(openGLContext));
  // Note: Phase Correlation Shader uses its OWN vertex shader, not the shared
  // one!
  if (phaseS->addVertexShader(PhaseCorrelationShader::vertexShader) &&
      phaseS->addFragmentShader(PhaseCorrelationShader::fragmentShader) &&
      phaseS->link()) {
    phaseShader = std::move(phaseS);
  }
}
