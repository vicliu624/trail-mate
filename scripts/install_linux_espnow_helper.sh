#!/bin/sh
set -eu
test "$(id -u)" = 0
test -r /usr/include/linux/nl80211.h
id pi >/dev/null
for dependency in python3 nmcli iw ip rfkill systemctl; do
    command -v "$dependency" >/dev/null
done
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test ! -e /var/lib/trailmate-espnow-probe/armed.json
install -d -m 755 /usr/local/libexec/trailmate-espnow
install -d -m 700 /var/lib/trailmate-espnow-probe
install -m 755 "$root/scripts/linux_espnow_guard.py" /usr/local/libexec/trailmate-espnow-guard
install -m 644 "$root/scripts/linux_espnow_probe.py" "$root/scripts/linux_espnow_helper.py" /usr/local/libexec/trailmate-espnow/
install -m 644 "$root"/packaging/linux/espnow/*.service "$root"/packaging/linux/espnow/*.timer "$root"/packaging/linux/espnow/*.socket /etc/systemd/system/
systemd-analyze verify /etc/systemd/system/trailmate-espnow-*.service /etc/systemd/system/trailmate-espnow-*.timer /etc/systemd/system/trailmate-espnow-*.socket
systemctl daemon-reload
systemctl enable --now trailmate-espnow-recover.timer trailmate-espnow-helper.socket
systemctl is-active trailmate-espnow-recover.timer trailmate-espnow-helper.socket
