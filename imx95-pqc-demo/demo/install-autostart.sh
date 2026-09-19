#!/usr/bin/env bash
# Install and enable the boot-time demo units. Run ON THE BOARD as root.
#
#   sudo bash install-autostart.sh            # serial console (default)
#   sudo bash install-autostart.sh --display  # serial console + DisplayPort
#   sudo bash install-autostart.sh --off      # disable, restore the getty
#
# The serial-console demo is the default because it needs nothing but the
# console cable. --display adds a renderer that mirrors the same demo onto a
# DisplayPort panel; it is additive because the renderer starts nothing of its
# own, it only follows the container logs and the Cortex-M7 ring that the
# serial unit brings up.
#
# wolfssl-demo.service is the older combined unit, which both renders and
# starts the demo. Running it next to these two gives two renderers fighting
# for the panel and two writers to the same container, so it is disabled here.
set -euo pipefail
UART_UNIT=/etc/systemd/system/wolfssl-demo-uart.service
DISPLAY_UNIT=/etc/systemd/system/wolfssl-display.service
M7_UNIT=/etc/systemd/system/wolfssl-m7.service
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ "${1:-}" = "--off" ]; then
    systemctl disable --now wolfssl-demo-uart.service 2>/dev/null || true
    systemctl disable --now wolfssl-display.service 2>/dev/null || true
    systemctl disable --now wolfssl-demo.service 2>/dev/null || true
    systemctl disable --now wolfssl-m7.service 2>/dev/null || true
    systemctl start getty@tty1.service 2>/dev/null || true
    sync
    echo "demo autostart disabled, getty on tty1 restored"
    exit 0
fi

install -m 0644 "$HERE/wolfssl-m7.service" "$M7_UNIT"
install -m 0644 "$HERE/wolfssl-demo-uart.service" "$UART_UNIT"
UNITS="wolfssl-demo-uart.service wolfssl-m7.service"
if [ "${1:-}" = "--display" ]; then
    install -m 0644 "$HERE/wolfssl-display.service" "$DISPLAY_UNIT"
    UNITS="$UNITS wolfssl-display.service"
else
    systemctl disable wolfssl-display.service 2>/dev/null || true
fi
systemctl disable wolfssl-demo.service 2>/dev/null || true
systemctl daemon-reload
systemctl enable $UNITS
# The demo's replay beat is a hard power cut, so nothing may sit in the page
# cache: an unsynced unit file is simply gone after the next cycle.
sync
echo "installed and enabled: $UNITS"
echo "the demo now starts on boot; power cycle is the whole replay"
echo "disable with: sudo bash $HERE/install-autostart.sh --off"
