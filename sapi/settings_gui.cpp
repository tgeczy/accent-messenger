#include "settings.h"
#include <sapi.h>
#include <sphelper.h>

using namespace messenger_sapi;
static ISpVoice* preview = nullptr;
static bool getValues(HWND dialog, Settings& s) {
    s.voice = int(SendDlgItemMessageW(dialog, 101, CB_GETCURSEL, 0, 0));
    s.inflection = int(SendDlgItemMessageW(dialog, 102, CB_GETCURSEL, 0, 0)) * 25;
    s.spacing = int(SendDlgItemMessageW(dialog, 103, CB_GETCURSEL, 0, 0));
    int* values[] = {&s.rate, &s.pitch, &s.volume};
    for (int i = 0; i < 3; i++) {
        BOOL valid = FALSE;
        auto value = GetDlgItemInt(dialog, 104 + i, &valid, FALSE);
        if (!valid || value > 100) {
            MessageBoxW(dialog, L"Enter a number from 0 to 100.", L"Accent Messenger",
                        MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dialog, 104 + i));
            return false;
        }
        *values[i] = int(value);
    }
    s.readDigits = IsDlgButtonChecked(dialog, 107) == BST_CHECKED;
    return s.voice >= 0 && s.spacing >= 0 && s.inflection >= 0;
}
static INT_PTR CALLBACK dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM) {
    if (message == WM_INITDIALOG) {
        auto s = readSettings();
        for (int n = 0; n < 10; n++) {
            auto label = L"V" + std::to_wstring(n) +
                         (n == 5   ? L" (default)"
                          : n == 0 ? L" (deepest)"
                          : n == 9 ? L" (sharpest)"
                                   : L"");
            SendDlgItemMessageW(dialog, 101, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(label.c_str()));
            label = std::to_wstring(n) + (n == 0 ? L" (shortest)" : L"");
            SendDlgItemMessageW(dialog, 103, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(label.c_str()));
        }
        for (auto label :
             {L"0 - Monotone", L"25 - Low", L"50 - Medium", L"75 - High", L"100 - Full (default)"})
            SendDlgItemMessageW(dialog, 102, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendDlgItemMessageW(dialog, 101, CB_SETCURSEL, s.voice, 0);
        SendDlgItemMessageW(dialog, 102, CB_SETCURSEL, s.inflection / 25, 0);
        SendDlgItemMessageW(dialog, 103, CB_SETCURSEL, s.spacing, 0);
        SetDlgItemInt(dialog, 104, s.rate, FALSE);
        SetDlgItemInt(dialog, 105, s.pitch, FALSE);
        SetDlgItemInt(dialog, 106, s.volume, FALSE);
        CheckDlgButton(dialog, 107, s.readDigits ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(dialog, 108,
                        L"Hello. This is Accent Messenger. One hundred, four, one, E.");
        SendDlgItemMessageW(dialog, 108, EM_SETLIMITTEXT, 4000, 0);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDOK:
        case 109: {
            Settings s;
            if (!getValues(dialog, s))
                return TRUE;
            bool shared = false;
            if (!writeSettings(s, &shared)) {
                MessageBoxW(dialog, L"The settings file could not be saved.", L"Accent Messenger",
                            MB_OK | MB_ICONERROR);
                return TRUE;
            }
            if (!shared)
                MessageBoxW(dialog,
                            L"Your personal settings were saved, but settings for other accounts "
                            L"and sign-in screens could not be updated. Reinstall Accent Messenger "
                            L"SAPI, then save again.",
                            L"Accent Messenger", MB_OK | MB_ICONWARNING);
            if (LOWORD(wParam) == IDOK) {
                EndDialog(dialog, IDOK);
                return TRUE;
            }
            HRESULT hr = S_OK;
            if (!preview) {
                hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_ISpVoice,
                                      reinterpret_cast<void**>(&preview));
                ISpObjectToken* token = nullptr;
                if (SUCCEEDED(hr))
                    hr = SpGetTokenFromId(
                        L"HKEY_LOCAL_"
                        L"MACHINE\\Software\\Microsoft\\Speech\\Voices\\Tokens\\AccentMessenger",
                        &token, FALSE);
                if (SUCCEEDED(hr))
                    hr = preview->SetVoice(token);
                if (token)
                    token->Release();
                if (FAILED(hr) && preview) {
                    preview->Release();
                    preview = nullptr;
                }
            }
            if (SUCCEEDED(hr)) {
                wchar_t text[4001];
                GetDlgItemTextW(dialog, 108, text, 4001);
                hr = preview->Speak(text, SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML,
                                    nullptr);
            }
            if (FAILED(hr))
                MessageBoxW(dialog,
                            L"The voice could not speak. Install Accent Messenger SAPI, then "
                            L"reopen this settings tool.",
                            L"Accent Messenger", MB_OK | MB_ICONERROR);
            return TRUE;
        }
        case 110:
            if (preview)
                preview->Speak(nullptr, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return 1;
    auto result = DialogBoxParamW(instance, MAKEINTRESOURCEW(1), nullptr, dialogProc, 0);
    if (preview) {
        preview->Speak(nullptr, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
        preview->Release();
    }
    CoUninitialize();
    return result == -1 ? 1 : 0;
}
