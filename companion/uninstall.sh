#!/bin/sh
set -eu
if [ "$(id -u)" = 0 ]; then echo 'Uninstall as your normal desktop user.' >&2; exit 1; fi
if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now deskbuddy-agent.service 2>/dev/null || true
fi
# The fallback runs as this user and has no children or GUI.
if command -v pkill >/dev/null 2>&1; then pkill -u "$(id -u)" -x deskbuddy-agent 2>/dev/null || true; fi
rm -f "$HOME/.local/bin/deskbuddy-agent" "$HOME/.config/systemd/user/deskbuddy-agent.service" "$HOME/.config/autostart/deskbuddy-agent.desktop"
if command -v systemctl >/dev/null 2>&1; then systemctl --user daemon-reload 2>/dev/null || true; fi
echo 'Companion removed. Revoke this device in Connected Devices. Credentials remain in ~/.config/deskbuddy-agent; remove that directory to erase them.'
