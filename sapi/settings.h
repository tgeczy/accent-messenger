#pragma once
#include <string>
#include <windows.h>

namespace messenger_sapi {
struct Settings {
    int voice = 5, inflection = 100, spacing = 0, rate = 50, pitch = 50, volume = 100;
    bool readDigits = false;
};
std::wstring modulePath(HMODULE module);
std::wstring settingsPath();
std::wstring machineSettingsPath();
// Registration prepares only the shared numeric-preferences directory.
HRESULT prepareMachineSettings();
Settings readSettings();
bool writeSettings(const Settings& settings, bool* machineSaved = nullptr);
std::string normalise(const std::wstring& text, bool digits, bool spell = false);
} // namespace messenger_sapi
