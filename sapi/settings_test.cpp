// Test the actual native dialog controls while the dialog remains hidden.
#include "settings_gui.cpp"
#include <cstdio>
#include <fstream>
int wmain(int argc, wchar_t** argv) {
    if (argc != 2)
        return 2;
    SetEnvironmentVariableW(L"ACCENT_MESSENGER_SETTINGS", argv[1]);
    const auto shared = std::wstring(argv[1]) + L".shared\\settings.toml";
    SetEnvironmentVariableW(L"ACCENT_MESSENGER_SETTINGS_MACHINE", shared.c_str());
    if (FAILED(prepareMachineSettings()))
        return 1;
    Settings initial;
    if (!writeSettings(initial))
        return 1;
    HWND dialog =
        CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), nullptr, dialogProc, 0);
    if (!dialog || IsWindowVisible(dialog))
        return 1;
    Settings values;
    bool success = getValues(dialog, values) && values.voice == 5 && values.inflection == 100 &&
                   values.spacing == 0 && values.volume == 100;
    SendDlgItemMessageW(dialog, 101, CB_SETCURSEL, 3, 0);
    SendDlgItemMessageW(dialog, 102, CB_SETCURSEL, 1, 0);
    SendDlgItemMessageW(dialog, 103, CB_SETCURSEL, 2, 0);
    SetDlgItemInt(dialog, 104, 65, FALSE);
    SetDlgItemInt(dialog, 105, 40, FALSE);
    SetDlgItemInt(dialog, 106, 80, FALSE);
    CheckDlgButton(dialog, 107, BST_CHECKED);
    success = success && getValues(dialog, values) && writeSettings(values);
    auto saved = readSettings();
    success = success && saved.voice == 3 && saved.inflection == 25 && saved.spacing == 2 &&
              saved.rate == 65 && saved.pitch == 40 && saved.volume == 80 && saved.readDigits;
    DestroyWindow(dialog);
    // A service account with no personal preferences sees the last shared save.
    auto absent = std::wstring(argv[1]) + L".absent";
    SetEnvironmentVariableW(L"ACCENT_MESSENGER_SETTINGS", absent.c_str());
    saved = readSettings();
    success = success && saved.voice == 3 && saved.inflection == 25 && saved.spacing == 2 &&
              saved.rate == 65 && saved.pitch == 40 && saved.volume == 80 && saved.readDigits;
    SetEnvironmentVariableW(L"ACCENT_MESSENGER_SETTINGS", argv[1]);
    {
        std::ofstream out(argv[1]);
        out << "Voice = 8\nRate = broken\nVolume = 999\nWordSpacing = 4\n";
    }
    saved = readSettings();
    success =
        success && saved.voice == 8 && saved.rate == 65 && saved.volume == 80 && saved.spacing == 4;
    // Oversized files are ignored before allocating or parsing their contents.
    {
        std::ofstream out(argv[1]);
        out << "Voice = 7\n" << std::string(65537, 'x');
    }
    success = success && readSettings().voice == 3;
    // A missing machine directory does not pretend the shared save succeeded.
    auto unavailable = shared + L".missing\\settings.toml";
    SetEnvironmentVariableW(L"ACCENT_MESSENGER_SETTINGS_MACHINE", unavailable.c_str());
    bool machineSaved = true;
    success = success && writeSettings(values, &machineSaved) && !machineSaved;
    success = success && readSettings().voice == 3;
    SetEnvironmentVariableW(L"ACCENT_MESSENGER_SETTINGS", absent.c_str());
    saved = readSettings();
    success = success && saved.voice == 5 && saved.rate == 50 && saved.volume == 100;

    puts(success ? "PASS: hidden dialog; shared saves; user precedence; invalid/missing/oversized "
                   "file fallback; partial save reporting."
                 : "FAIL: native settings dialog");
    return success ? 0 : 1;
}
