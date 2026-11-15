#!/bin/sh
set -eu
if [ "$(id -u)" = 0 ]; then echo 'Install as your normal desktop user.' >&2; exit 1; fi
cd "$(dirname "$0")"
expected=$(cat ARCHITECTURE)
if [ "$(uname -m)" != "$expected" ]; then echo "This package is for $expected, this computer is $(uname -m)." >&2; exit 1; fi
./deskbuddy-agent --version
install -d -m 700 "$HOME/.local/bin" "$HOME/.config/systemd/user"
install -m 755 deskbuddy-agent "$HOME/.local/bin/deskbuddy-agent"
install -m 644 deskbuddy-agent.service "$HOME/.config/systemd/user/deskbuddy-agent.service"
if command -v systemctl >/dev/null 2>&1 && systemctl --user is-active --quiet graphical-session.target; then
    systemctl --user daemon-reload
    systemctl --user enable deskbuddy-agent.service
    echo 'Installed systemd user service. Pair first, then run: systemctl --user start deskbuddy-agent'
else
    install -d -m 700 "$HOME/.config/autostart"
    printf '[Desktop Entry]\nType=Application\nName=DeskBuddy Companion\nExec="%s/.local/bin/deskbuddy-agent"\nTerminal=false\n' "$HOME" > "$HOME/.config/autostart/deskbuddy-agent.desktop"
    echo 'Installed XDG desktop-session autostart fallback. Pair first; it will start at your next login.'
fi
