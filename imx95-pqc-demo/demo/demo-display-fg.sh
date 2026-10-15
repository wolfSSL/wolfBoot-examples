#!/usr/bin/env bash
# Render the two panes on the DisplayPort panel, in the foreground so systemd
# owns the renderer's lifetime. Runs ON THE BOARD as root.
#
# This is the display half of the demo only. It starts nothing: the benchmark
# container and the Cortex-M7 are brought up by demo-uart.sh (or demo-run.sh),
# and this just follows what they produce. That split is what lets the screen
# be a second view of the serial console rather than a competing demo.
set -uo pipefail

DEMO="$(cd "$(dirname "$0")" && pwd)"

# Torizon's kernel has no fbdev emulation, so text written to a VT reaches no
# display: the renderer has to own the DisplayPort through KMS. Take the screen
# away from everything else that wants it - the framebuffer console redrawing
# over the KMS output, and Torizon's pairing overlay - then render as the sole
# DRM master. The getty is handled by the unit's Conflicts= line.
for v in /sys/class/vtconsole/vtcon*; do
    if grep -qi 'frame buffer' "$v/name" 2>/dev/null; then
        echo 0 > "$v/bind" 2>/dev/null || true
    fi
done
docker stop torizon-easy-pairing-bash-1 >/dev/null 2>&1 || true

# stderr is left alone on purpose: the DRM path is the most failure-prone part
# of the demo (hotplug, EDID, DRM-master contention), and discarding it turns a
# black screen into a silent restart loop with nothing in the journal.
exec env RENDER=drm python3 "$DEMO/twopane.py"
