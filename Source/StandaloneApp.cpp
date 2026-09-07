/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone

#if ! JUCE_MODULE_AVAILABLE_juce_audio_utils
 #error To compile AudioUnitv3 and/or Standalone plug-ins, you need to add the juce_audio_utils and juce_audio_devices modules!
#endif

#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>

#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_gui_basics/native/juce_WindowsHooks_windows.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "PluginProcessor.h"

namespace juce
{

//==============================================================================
class SleekTitleBarButton : public juce::Button
{
public:
    enum Type { Minimize, Maximise, Close };

    SleekTitleBarButton (Type typeIn)
        : juce::Button (typeIn == Close ? "close" : (typeIn == Maximise ? "maximise" : "minimise")),
          buttonType (typeIn)
    {
    }

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Background highlight on hover / click
        if (buttonType == Close)
        {
            if (shouldDrawButtonAsDown)
                g.fillAll (juce::Colour (0xFFBF0F1D));
            else if (shouldDrawButtonAsHighlighted)
                g.fillAll (juce::Colour (0xFFE81123));
        }
        else
        {
            if (shouldDrawButtonAsDown)
                g.fillAll (juce::Colour (0x4000E5FF));
            else if (shouldDrawButtonAsHighlighted)
                g.fillAll (juce::Colour (0x2000E5FF));
        }

        // Icon colour
        juce::Colour iconColour;
        if (buttonType == Close && (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown))
            iconColour = juce::Colours::white;
        else if (shouldDrawButtonAsHighlighted)
            iconColour = juce::Colour (0xFF00E5FF);
        else
            iconColour = juce::Colour (0xFFA0B0B8);

        g.setColour (iconColour);

        auto cx = bounds.getCentreX();
        auto cy = bounds.getCentreY();

        if (buttonType == Minimize)
        {
            // Crisp horizontal line
            g.fillRect (cx - 5.0f, cy + 3.0f, 10.0f, 1.4f);
        }
        else if (buttonType == Maximise)
        {
            if (getToggleState())
            {
                // Restore icon (overlapping squares)
                g.drawRect (cx - 2.5f, cy - 4.5f, 7.0f, 7.0f, 1.2f);
                g.setColour (shouldDrawButtonAsHighlighted ? juce::Colour (0xFF142430) : juce::Colour (0xFF0A0A0E));
                g.fillRect (cx - 4.5f, cy - 2.5f, 7.0f, 7.0f);
                g.setColour (iconColour);
                g.drawRect (cx - 4.5f, cy - 2.5f, 7.0f, 7.0f, 1.2f);
            }
            else
            {
                // Clean single maximize / fullscreen square
                g.drawRect (cx - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.3f);
            }
        }
        else if (buttonType == Close)
        {
            // Clean diagonal cross
            juce::Path p;
            p.addLineSegment ({ cx - 4.5f, cy - 4.5f, cx + 4.5f, cy + 4.5f }, 1.3f);
            p.addLineSegment ({ cx + 4.5f, cy - 4.5f, cx - 4.5f, cy + 4.5f }, 1.3f);
            g.strokePath (p, juce::PathStrokeType (1.3f));
        }
    }

private:
    Type buttonType;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SleekTitleBarButton)
};

//==============================================================================
class SleekLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SleekLookAndFeel()
    {
        setColour (juce::DocumentWindow::textColourId, juce::Colour (0xFF00E5FF)); // Cyan text
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xFF1A1A24)); // Dark popup menu
        setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xFF00E5FF).withAlpha(0.2f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

        // Options button styling
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF14141E));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF00E5FF).withAlpha(0.3f));
        setColour (juce::TextButton::textColourOffId, juce::Colour (0xFF00E5FF));
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g,
                                     int w, int h, int titleSpaceX, int titleSpaceW,
                                     const juce::Image* icon, bool drawTitleTextOnLeft) override
    {
        // Sleek dark background
        g.fillAll (juce::Colour (0xFF0A0A0E));

        // Draw title
        g.setColour (window.findColour (juce::DocumentWindow::textColourId));
        g.setFont (juce::Font (h * 0.45f, juce::Font::bold));
        g.drawText (window.getName(), titleSpaceX, 0, titleSpaceW, h, juce::Justification::centred, true);
    }

    void drawMenuBarBackground (juce::Graphics& g, int width, int height,
                                bool, juce::MenuBarComponent&) override
    {
        g.fillAll (juce::Colour (0xFF0A0A0E)); // Match title bar
    }

    void drawMenuBarItem (juce::Graphics& g, int width, int height,
                          int itemIndex, const juce::String& itemText,
                          bool isMouseOverItem, bool isMenuOpen,
                          bool /*isMouseOverMenuBar*/, juce::MenuBarComponent&) override
    {
        if (isMouseOverItem || isMenuOpen)
        {
            g.fillAll (juce::Colour (0xFF00E5FF).withAlpha (0.15f));
        }

        g.setFont (juce::Font (height * 0.6f));
        g.setColour (isMouseOverItem || isMenuOpen ? juce::Colours::white : juce::Colour(0xFFBBBBBB));
        g.drawText (itemText, 0, 0, width, height, juce::Justification::centred, true);
    }

    juce::Button* createDocumentWindowButton (int buttonType) override
    {
        if (buttonType == juce::DocumentWindow::closeButton)
            return new SleekTitleBarButton (SleekTitleBarButton::Close);
        if (buttonType == juce::DocumentWindow::minimiseButton)
            return new SleekTitleBarButton (SleekTitleBarButton::Minimize);
        if (buttonType == juce::DocumentWindow::maximiseButton)
            return new SleekTitleBarButton (SleekTitleBarButton::Maximise);

        return nullptr;
    }

    void positionDocumentWindowButtons (juce::DocumentWindow&,
                                        int titleBarX, int titleBarY,
                                        int titleBarW, int titleBarH,
                                        juce::Button* minimiseButton,
                                        juce::Button* maximiseButton,
                                        juce::Button* closeButton,
                                        bool positionTitleBarButtonsOnLeft) override
    {
        auto buttonW = juce::jmax (45, static_cast<int> (titleBarH * 1.5f));

        auto x = positionTitleBarButtonsOnLeft ? titleBarX
                                               : titleBarX + titleBarW - buttonW;

        if (closeButton != nullptr)
        {
            closeButton->setBounds (x, titleBarY, buttonW, titleBarH);
            x += positionTitleBarButtonsOnLeft ? buttonW : -buttonW;
        }

        if (positionTitleBarButtonsOnLeft)
            std::swap (minimiseButton, maximiseButton);

        if (maximiseButton != nullptr)
        {
            maximiseButton->setBounds (x, titleBarY, buttonW, titleBarH);
            x += positionTitleBarButtonsOnLeft ? buttonW : -buttonW;
        }

        if (minimiseButton != nullptr)
        {
            minimiseButton->setBounds (x, titleBarY, buttonW, titleBarH);
        }
    }
};

//==============================================================================
class FullscreenKeyListener : public KeyListener
{
public:
    FullscreenKeyListener (DocumentWindow& w) : window (w) {}

    bool keyPressed (const KeyPress& key, Component*) override
    {
        if (key.getKeyCode() == KeyPress::F11Key ||
            (key.getKeyCode() == KeyPress::returnKey && key.getModifiers().isAltDown()))
        {
            window.setFullScreen (! window.isFullScreen());
            return true;
        }
        if (key.getKeyCode() == KeyPress::escapeKey && window.isFullScreen())
        {
            window.setFullScreen (false);
            return true;
        }
        return false;
    }

private:
    DocumentWindow& window;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullscreenKeyListener)
};

//==============================================================================
class StandaloneFilterApp final : public JUCEApplication,
                                  public ChangeListener
{
public:
    StandaloneFilterApp()
    {
        PropertiesFile::Options options;

        options.applicationName     = CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const String getApplicationName() override              { return CharPointer_UTF8 (JucePlugin_Name); }
    const String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return true; }
    void anotherInstanceStarted (const String&) override    {}

    StandaloneFilterWindow* createWindow()
    {
        auto* window = new StandaloneFilterWindow (getApplicationName(),
                                           LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
                                           createPluginHolder());
                                           
        // Apply custom branded look and feel
        window->setLookAndFeel (&sleekLookAndFeel);
                                           
        // Kill the native OS title bar and use our custom one!
        window->setUsingNativeTitleBar (false);
        
        // Enable All Title Bar Buttons: [Minimize] [Maximize / Fullscreen] [Close]
        window->setTitleBarButtonsRequired (DocumentWindow::allButtons, false);
        
        // Force the properties file to be created so we don't lose audio settings!
        if (auto* props = appProperties.getUserSettings())
        {
            props->setValue("Initialized", true);
            props->saveIfNeeded();
        }
        
        return window;
    }

    std::unique_ptr<StandalonePluginHolder> createPluginHolder()
    {
        constexpr auto autoOpenMidiDevices =
       #if (JUCE_ANDROID || JUCE_IOS) && ! JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE
                true;
       #else
                false;
       #endif


       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig (channels, juce::numElementsInArray (channels));
       #else
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        auto holder = std::make_unique<StandalonePluginHolder> (appProperties.getUserSettings(),
                                                         false,
                                                         String{},
                                                         nullptr,
                                                         channelConfig,
                                                         autoOpenMidiDevices);
                                                         
        // Force audio input to be unmuted by default! JUCE mutes it by default to avoid 
        // speaker-to-mic feedback loops, but we are a visualizer and *need* the input!
        holder->getMuteInputValue().setValue(false);
        
        return holder;
    }

    //==============================================================================
    void initialise (const String&) override
    {
        mainWindow = rawToUniquePtr (createWindow());

        if (mainWindow != nullptr)
        {
           #if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
           #endif

            mainWindow->centreWithSize (1280, 720);
            mainWindow->setVisible (true);
            mainWindow->toFront (true);

            fullscreenKeyListener = std::make_unique<FullscreenKeyListener>(*mainWindow);
            mainWindow->addKeyListener (fullscreenKeyListener.get());

            if (auto* holder = mainWindow->pluginHolder.get())
            {
                holder->deviceManager.addChangeListener(this);
                autoDetectOutputLoopback(holder);
            }
            
            appProperties.saveIfNeeded();
        }
        else
        {
            pluginHolder = createPluginHolder();
            if (pluginHolder != nullptr)
            {
                pluginHolder->deviceManager.addChangeListener(this);
                autoDetectOutputLoopback(pluginHolder.get());
            }
        }
    }

    void autoDetectOutputLoopback(StandalonePluginHolder* holder)
    {
        if (holder == nullptr) return;

        auto& dm = holder->deviceManager;
        dm.setCurrentAudioDeviceType("Windows Audio", true);

        auto* type = dm.getCurrentDeviceTypeObject();
        if (type == nullptr) return;

        type->scanForDevices();
        auto outDevs = type->getDeviceNames(false);
        int defaultOutIdx = type->getDefaultDeviceIndex(false);
        String defaultOutName = (defaultOutIdx >= 0 && defaultOutIdx < outDevs.size()) 
                                ? outDevs[defaultOutIdx] : (outDevs.size() > 0 ? outDevs[0] : String());

        auto inDevs = type->getDeviceNames(true);
        String chosenInput;

        // 1. Minifuse 2 dedicated loopback (highest priority for user's hardware)
        for (const auto& name : inDevs)
        {
            if (name.containsIgnoreCase("Minifuse") && 
               (name.containsIgnoreCase("Loopback") || name.containsIgnoreCase("Mix 1/2") || name.containsIgnoreCase("Mix 5/6")))
            {
                chosenInput = name;
                break;
            }
        }

        // 2. Direct match for default output loopback
        if (chosenInput.isEmpty() && defaultOutName.isNotEmpty())
        {
            for (const auto& name : inDevs)
            {
                if (name.containsIgnoreCase("loopback") && name.containsIgnoreCase(defaultOutName))
                {
                    chosenInput = name;
                    break;
                }
            }
        }

        // 3. Any device with "Loopback" in the name
        if (chosenInput.isEmpty())
        {
            for (const auto& name : inDevs)
            {
                if (name.containsIgnoreCase("loopback"))
                {
                    chosenInput = name;
                    break;
                }
            }
        }

        // 4. VoiceMeeter or Virtual Cable if active
        if (chosenInput.isEmpty())
        {
            for (const auto& name : inDevs)
            {
                if (name.containsIgnoreCase("VoiceMeeter Output") || name.containsIgnoreCase("CABLE Output"))
                {
                    chosenInput = name;
                    break;
                }
            }
        }

        // Apply device setup
        AudioDeviceManager::AudioDeviceSetup setup;
        dm.getAudioDeviceSetup(setup);
        if (chosenInput.isNotEmpty())
            setup.inputDeviceName = chosenInput;
        if (defaultOutName.isNotEmpty())
            setup.outputDeviceName = defaultOutName;
        setup.inputChannels.setRange(0, 2, true);
        setup.useDefaultInputChannels = false;
        setup.outputChannels.setRange(0, 2, true);
        setup.useDefaultOutputChannels = false;

        dm.setAudioDeviceSetup(setup, true);

        // Update processor input display name
        if (auto* proc = dynamic_cast<WraithFormAudioProcessor*>(holder->processor.get()))
        {
            if (auto* device = dm.getCurrentAudioDevice())
                proc->currentStandaloneInputName = device->getName();
            else if (chosenInput.isNotEmpty())
                proc->currentStandaloneInputName = chosenInput;
        }

        // Ensure audio input is unmuted for visualization
        holder->getMuteInputValue().setValue(false);

        // Save immediately
        holder->savePluginState();
        appProperties.saveIfNeeded();
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        StandalonePluginHolder* holder = (mainWindow != nullptr && mainWindow->pluginHolder != nullptr)
                                             ? mainWindow->pluginHolder.get()
                                             : pluginHolder.get();

        if (holder != nullptr)
        {
            holder->savePluginState();

            if (auto* proc = dynamic_cast<WraithFormAudioProcessor*>(holder->processor.get()))
            {
                if (auto* device = holder->deviceManager.getCurrentAudioDevice())
                    proc->currentStandaloneInputName = device->getName();
                else
                    proc->currentStandaloneInputName = "No Input Device";
            }
        }

        appProperties.saveIfNeeded();
    }

    void shutdown() override
    {
        if (mainWindow != nullptr && fullscreenKeyListener != nullptr)
            mainWindow->removeKeyListener (fullscreenKeyListener.get());
        fullscreenKeyListener = nullptr;
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, []()
            {
                if (auto app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

protected:
    ApplicationProperties appProperties;
    SleekLookAndFeel sleekLookAndFeel;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;
    std::unique_ptr<FullscreenKeyListener> fullscreenKeyListener;

private:
    std::unique_ptr<StandalonePluginHolder> pluginHolder;
};

} // namespace juce

#if JucePlugin_Build_Standalone && JUCE_IOS

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wmissing-prototypes")

using namespace juce;

bool JUCE_CALLTYPE juce_isInterAppAudioConnected()
{
    if (auto holder = StandalonePluginHolder::getInstance())
        return holder->isInterAppAudioConnected();

    return false;
}

void JUCE_CALLTYPE juce_switchToHostApplication()
{
    if (auto holder = StandalonePluginHolder::getInstance())
        holder->switchToHostApplication();
}

Image JUCE_CALLTYPE juce_getIAAHostIcon (int size)
{
    if (auto holder = StandalonePluginHolder::getInstance())
        return holder->getIAAHostIcon (size);

    return Image();
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE

#endif

// Since this IS the custom app, we must define the creator function here.
JUCE_CREATE_APPLICATION_DEFINE (juce::StandaloneFilterApp)

// Let the original JUCE wrapper define WinMain, so we don't get a duplicate definition error.
#define JUCE_USE_CUSTOM_PLUGIN_STANDALONE_ENTRYPOINT 1

#endif
