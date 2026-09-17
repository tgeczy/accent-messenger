#include "settings.h"
#include <aclapi.h>
#include <algorithm>
#include <fstream>
#include <shlobj.h>
#include <sstream>

namespace messenger_sapi {
std::wstring modulePath(HMODULE module) {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(module, path, 32768);
    return n && n < 32768 ? std::wstring(path, n) : L"";
}
static std::wstring settingsFilePath(int folder, const wchar_t* environment) {
    wchar_t overridePath[32768];
    DWORD n = GetEnvironmentVariableW(environment, overridePath, 32768);
    if (n && n < 32768)
        return overridePath;
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, folder, nullptr, 0, appdata)))
        return L"";
    return std::wstring(appdata) + L"\\Accent Messenger\\settings.toml";
}
std::wstring settingsPath() {
    return settingsFilePath(CSIDL_APPDATA, L"ACCENT_MESSENGER_SETTINGS");
}
std::wstring machineSettingsPath() {
    return settingsFilePath(CSIDL_COMMON_APPDATA, L"ACCENT_MESSENGER_SETTINGS_MACHINE");
}
HRESULT prepareMachineSettings() {
    auto path = machineSettingsPath();
    if (path.empty())
        return E_FAIL;
    auto dir = path.substr(0, path.find_last_of(L"\\/"));
    auto status = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (status != ERROR_SUCCESS && status != ERROR_ALREADY_EXISTS && status != ERROR_FILE_EXISTS)
        return HRESULT_FROM_WIN32(status);
    auto attributes = GetFileAttributesW(dir.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof sid;
    if (!CreateWellKnownSid(WinBuiltinUsersSid, nullptr, sid, &sidSize))
        return HRESULT_FROM_WIN32(GetLastError());
    PACL oldAcl = nullptr, newAcl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    status = GetNamedSecurityInfoW(const_cast<wchar_t*>(dir.c_str()), SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr,
                                   &descriptor);
    if (status != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(status);
    EXPLICIT_ACCESSW entry{};
    // Match Panthera: users may update preferences, not DLLs or speech data.
    entry.grfAccessPermissions =
        FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
    status = SetEntriesInAclW(1, &entry, oldAcl, &newAcl);
    if (status == ERROR_SUCCESS)
        status =
            SetNamedSecurityInfoW(const_cast<wchar_t*>(dir.c_str()), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl, nullptr);
    if (newAcl)
        LocalFree(newAcl);
    LocalFree(descriptor);
    return HRESULT_FROM_WIN32(status);
}
static void readSettingsFile(const std::wstring& path, Settings& s) {
    // A shared file can be hand-edited. Bound input before allocating, and allow
    // the settings tool to atomically replace it while another process reads.
    HANDLE input = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(input, &size) || size.QuadPart > 65536 || size.QuadPart < 0) {
        CloseHandle(input);
        return;
    }
    std::string bytes(size_t(size.QuadPart), '\0');
    DWORD count = 0;
    bool okay =
        bytes.empty() || (ReadFile(input, bytes.data(), DWORD(bytes.size()), &count, nullptr) &&
                          count == bytes.size());
    CloseHandle(input);
    if (!okay)
        return;
    if (bytes.compare(0, 3, "\xef\xbb\xbf") == 0)
        bytes.erase(0, 3);
    std::istringstream file(bytes);
    std::string line;
    size_t read = 0;
    while (std::getline(file, line) && (read += line.size()) < 65536) {
        line = line.substr(0, line.find('#'));
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return c <= 32; }),
                  key.end());
        std::istringstream value(line.substr(eq + 1));
        int v;
        std::string extra;
        if (!(value >> v) || (value >> extra))
            continue;
        if (key == "Voice" && v >= 0 && v <= 9)
            s.voice = v;
        if (key == "Inflection" && v >= 0 && v <= 100 && v % 25 == 0)
            s.inflection = v;
        if (key == "WordSpacing" && v >= 0 && v <= 9)
            s.spacing = v;
        if (key == "Rate" && v >= 0 && v <= 100)
            s.rate = v;
        if (key == "Pitch" && v >= 0 && v <= 100)
            s.pitch = v;
        if (key == "Volume" && v >= 0 && v <= 100)
            s.volume = v;
        if (key == "ReadDigits" && (v == 0 || v == 1))
            s.readDigits = v != 0;
    }
}
Settings readSettings() {
    Settings s;
    // Defaults, then valid machine values, then valid personal values.
    // Invalid/missing personal entries leave the machine value intact.
    readSettingsFile(machineSettingsPath(), s);
    readSettingsFile(settingsPath(), s);
    return s;
}
static bool writeSettingsFile(const std::wstring& path, const Settings& s, bool createDirectory) {
    if (path.empty())
        return false;
    auto dir = path.substr(0, path.find_last_of(L"\\/"));
    if (createDirectory && SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr) != ERROR_SUCCESS &&
        GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;
    auto temp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        out << "# Accent Messenger SAPI voice settings\n"
            << "Voice = " << s.voice << "\nInflection = " << s.inflection
            << "\nWordSpacing = " << s.spacing << "\nRate = " << s.rate << "\nPitch = " << s.pitch
            << "\nVolume = " << s.volume << "\nReadDigits = " << int(s.readDigits) << '\n';
        out.flush();
        if (!out) {
            out.close();
            DeleteFileW(temp.c_str());
            return false;
        }
    }
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(temp.c_str());
    return false;
}
bool writeSettings(const Settings& s, bool* machineSaved) {
    if (machineSaved)
        *machineSaved = false;
    if (!writeSettingsFile(settingsPath(), s, true))
        return false;
    // Registration/installation creates the shared folder with suitable ACLs.
    // A failed shared save must not discard the person's own preferences.
    bool shared = writeSettingsFile(machineSettingsPath(), s, false);
    if (machineSaved)
        *machineSaved = shared;
    return true;
}
} // namespace messenger_sapi
