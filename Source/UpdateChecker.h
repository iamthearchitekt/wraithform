#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

/**
    Helper class to check for updates against the GitHub API.
    Runs in a background thread to avoid blocking the UI.
*/
class UpdateChecker : public juce::Thread {
public:
  UpdateChecker() : juce::Thread("Update Checker") {}

  struct UpdateInfo {
    bool updateAvailable = false;
    juce::String latestVersion;
    juce::String downloadUrl;
  };

  void run() override {
    juce::URL url("https://api.github.com/repos/iamthearchitekt/wraithform/"
                  "releases/latest");

    // Add a User-Agent header which GitHub requires
    juce::StringPairArray headers;
    headers.set("User-Agent", "WraithForm-App");

    auto options =
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders(headers.toString())
            .withConnectionTimeoutMs(5000);

    if (auto stream = url.createInputStream(options)) {
      auto response = stream->readEntireStreamAsString();
      auto json = juce::JSON::parse(response);

      if (json.isObject()) {
        juce::String latestTag = json.getProperty("tag_name", "").toString();
        juce::String downloadUrl =
            json.getProperty(
                    "html_url",
                    "https://github.com/iamthearchitekt/wraithform/releases")
                .toString();

        if (latestTag.isNotEmpty()) {
          const juce::String currentVersion = ProjectInfo::versionString;

          // Simple version comparison (e.g., "1.0.4" vs "1.0.5")
          // We strip "v" if present
          auto cleanTag = latestTag.startsWithIgnoreCase("v")
                              ? latestTag.substring(1)
                              : latestTag;

          if (compareVersions(cleanTag, currentVersion) > 0) {
            juce::ScopedLock sl(lock);
            info.updateAvailable = true;
            info.latestVersion = latestTag;
            info.downloadUrl = downloadUrl;
          }
        }
      }
    }
  }

  UpdateInfo getLatestInfo() {
    juce::ScopedLock sl(lock);
    return info;
  }

private:
  /**
      Returns > 0 if v1 > v2
      Returns 0 if v1 == v2
      Returns < 0 if v1 < v2
  */
  static int compareVersions(const juce::String &v1, const juce::String &v2) {
    juce::StringArray parts1, parts2;
    parts1.addTokens(v1, ".", "");
    parts2.addTokens(v2, ".", "");

    for (int i = 0; i < juce::jmax(parts1.size(), parts2.size()); ++i) {
      int p1 = (i < parts1.size()) ? parts1[i].getIntValue() : 0;
      int p2 = (i < parts2.size()) ? parts2[i].getIntValue() : 0;

      if (p1 != p2)
        return p1 - p2;
    }

    return 0;
  }

  juce::CriticalSection lock;
  UpdateInfo info;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateChecker)
};
