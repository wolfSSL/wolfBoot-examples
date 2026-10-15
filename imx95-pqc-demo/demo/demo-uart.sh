#!/usr/bin/env bash
# Single-stream demo on the serial console. Runs ON THE BOARD as root.
#
# The DisplayPort renderer can only start once Linux is up, which is long after
# the interesting part: by then wolfBoot has already verified and booted the
# kernel, and the pane can only replay that from the DDR ring. On the UART there
# is nothing to replay - wolfBoot's own ML-DSA-87 verification prints live,
# seconds after power-on, and everything below simply continues the same stream:
#
#   wolfBoot (A55)  verified boot of Linux      - printed by wolfBoot itself
#   [M7 ]           wolfBoot ML-DSA-87 -> Zephyr on the Cortex-M7
#   [A55]           wolfCrypt ML-KEM / ML-DSA benchmarks, in a container
#
# Each source is prefixed so one scrolling stream stays readable, and output
# goes to the console the kernel already owns, so a plain `screen`/`picocom` on
# the host records the whole demo with no display, no KMS and no X.
set -uo pipefail

DEMO=${DEMO:-/home/torizon/demo}
MEMTOOL=${MEMTOOL:-/home/torizon/bin/memtool}
CONSOLE_ADDR=${CONSOLE_ADDR:-0x80F00000}
STATUS_ADDR=${STATUS_ADDR:-0x80F10000}
# wolfBoot (BL33) mirrors its console into this DDR ring (IMX95_LOG_RING=1), so
# its verified-boot lines can still be read after the handoff to Linux.
A55_RING_ADDR=${A55_RING_ADDR:-0x80F20000}
M7_CORE_MHZ=${M7_CORE_MHZ:-800}
CONTAINER=${CONTAINER:-wolfcrypt-pqc}
INTERVAL=${INTERVAL:-0.5}
SETTLE=${SETTLE:-3}

# systemd writes its status lines as "[<green>  OK  <reset>] ...". Demo output
# that lands between those two codes inherits the green, so every line below
# starts with an explicit reset rather than trusting the console's state.
R=$'\033[0m'
C_M7=$'\033[1;36m'
C_A55=$'\033[1;33m'
C_OK=$'\033[1;32m'
C_HDR=$'\033[1;37m'

# Stop the kernel from writing to this console for the rest of the demo. Its
# late-boot chatter (USB, Bluetooth, wlan scans) otherwise interleaves with the
# output and there is nothing in it the demo wants to show. The messages still
# reach the journal, so nothing is lost - dmesg -n 8 restores them.
dmesg -n 1 2>/dev/null || true

# Let the last of systemd's own status lines drain before the banner, so the
# demo starts on a clean console instead of halfway through someone else's line.
sleep "$SETTLE"

printf '%s\n' "$R"
echo "=================================================================="
echo " NXP i.MX95 - post-quantum verified boot on both clusters"
echo "   Cortex-A55  wolfBoot ML-DSA-87 -> Linux   (replayed below)"
echo "   Cortex-M7   wolfBoot ML-DSA-87 -> Zephyr  [M7 ]"
echo "   Cortex-A55  wolfCrypt ML-KEM / ML-DSA     [A55]"
echo "=================================================================="
echo

# Replay the A55 verified-boot lines.
#
# They print live, seconds after power-on, but by the time anyone is watching
# they have scrolled away under the kernel's own boot messages. wolfBoot mirrors
# its console into a DDR ring precisely so this moment can be shown again, at
# the point in the demo where someone is actually looking at the screen.
a55_verified_boot() {
    local ring
    ring=$("$MEMTOOL" con "$A55_RING_ADDR" 2>/dev/null) || return 0
    [ -n "$ring" ] || return 0

    printf '%s\n' "$R"
    echo "${C_HDR}+---------------------------------------------------------------+${R}"
    echo "${C_HDR}| Cortex-A55: what wolfBoot did before Linux started             |${R}"
    echo "${C_HDR}+---------------------------------------------------------------+${R}"
    # wolfBoot's console writes CRLF; strip the CR or it prints as ^M here.
    printf '%s' "$ring" | tr -d '\r' \
      | grep -aE "Boot partition:|Checking image integrity|Verifying image signature|ML-DSA|MlDsaKey|Firmware Valid" \
      | awk '!seen[$0]++' \
      | while IFS= read -r line; do
            case "$line" in
                *"returned OK"|*"Firmware Valid"*) printf '%s  %s%s\n' "$C_OK" "$line" "$R" ;;
                *) printf '%s  %s%s\n' "$C_A55" "$line" "$R" ;;
            esac
        done
    echo "${C_HDR}+---------------------------------------------------------------+${R}"
    echo
}
a55_verified_boot

# The compose network does not survive a power cut while a container created by
# an earlier run does, so "up -d" alone fails with "network not found" and the
# benchmark half of the stream stays empty. The demo's replay beat IS a power
# cut, so tear down first.
( cd "$DEMO" && docker compose down --remove-orphans >/dev/null 2>&1 || true )
# One pass, not a loop: the demo's beat is a board reset, so the stream should
# end rather than scroll benchmark cycles forever. LOOP=0 makes the container's
# entrypoint break after a single cycle and exit, which is also what lets the
# reader below know the A55 half is finished.
( cd "$DEMO" && LOOP=0 RESTART_POLICY=no docker compose up -d >/dev/null 2>&1 ) \
    || echo "[A55] container failed to start"

# The M7 is normally released by wolfssl-m7.service, which systemd starts in
# parallel with this unit. Go through systemd where that unit exists, so the two
# cannot both load the payload: the second load lands in DDR under a core that
# is already executing it, and the app faults on a corrupted heap seconds later.
# m7-start.sh takes a lock as well, for a board without the unit.
if [ "$(cat /sys/class/remoteproc/remoteproc1/state 2>/dev/null)" != "running" ]; then
    if systemctl cat wolfssl-m7.service >/dev/null 2>&1; then
        systemctl start wolfssl-m7.service >/dev/null 2>&1 || true
    else
        bash "$DEMO/m7-start.sh" >/dev/null 2>&1 || true
    fi
fi

# memtool dumps the whole ring each call, so track what has already been shown
# and print only what is new, splitting on lines so the prefix lands correctly.
m7_stream() {
    local shown=0 out len chunk complete idle=0
    while true; do
        # Stop once the payload says its benchmark is done, so this half of the
        # stream ends instead of relaying heartbeats indefinitely.
        if [ -n "${M7_DONE:-}" ]; then
            return 0
        fi
        out=$("$MEMTOOL" con "$CONSOLE_ADDR" 2>/dev/null) || { sleep "$INTERVAL"; continue; }
        len=${#out}
        if [ "$len" -gt "$shown" ]; then
            chunk=${out:$shown}
            case "$chunk" in
                *$'\n'*)
                    # Everything up to the final newline is complete; whatever
                    # follows is a partial line still being written, so leave it
                    # for the next poll rather than printing half of it.
                    complete=${chunk%$'\n'*}
                    shown=$(( shown + ${#complete} + 1 ))
                    idle=0
                    ;;
                *)
                    # No newline yet. Hold the tail, but do not hold it forever:
                    # the last line of a run never gets one.
                    idle=$(( idle + 1 ))
                    if [ "$idle" -ge 6 ]; then
                        complete=$chunk
                        shown=$len
                        idle=0
                    else
                        complete=""
                    fi
                    ;;
            esac
            if [ -n "$complete" ]; then
                printf '%s\n' "$complete" | while IFS= read -r line; do
                    case "$line" in
                        *[![:space:]]*) printf '%s%s[M7 ]%s %s\n' "$R" "$C_M7" "$R" "$line" ;;
                    esac
                done
                case "$complete" in
                    *"end of Cortex-M7 benchmark"*) M7_DONE=1 ;;
                esac
            fi
        elif [ "$len" -lt "$shown" ]; then
            printf '%s%s[M7 ]%s --- ring restarted ---\n' "$R" "$C_M7" "$R"
            shown=0
            idle=0
        fi
        sleep "$INTERVAL"
    done
}

# Only result lines. The banner, the CPU flags and the dashed rules are noise on
# a demo screen, and the Cycles/op columns derive from the 24 MHz generic timer
# rather than the core clock, so they are wrong by roughly 75x and must never be
# shown - ops/sec and MB/s are the correct numbers.
# The raw line is about 95 characters, which wraps on an 80-column console and
# breaks the column the eye is actually following. Condense to
# "<algorithm> <operation>   <rate>" and drop the redundant parameter fields, so
# a recording stays readable at any width.
bench_stream() {
    docker logs -f --tail 0 "$CONTAINER" 2>&1 | awk -v R="$R" -v C="$C_A55" '
        function shorten(s) {
            gsub(/\[ *SECP256R1\]/, "P-256", s)
            gsub(/P-256 +256/, "P-256", s)
            gsub(/ML-KEM +512 +128/,  "ML-KEM-512",  s)
            gsub(/ML-KEM +768 +192/,  "ML-KEM-768",  s)
            gsub(/ML-KEM +1024 +256/, "ML-KEM-1024", s)
            gsub(/ML-DSA +44/,        "ML-DSA-44",   s)
            gsub(/ML-DSA +65/,        "ML-DSA-65",   s)
            gsub(/ML-DSA +87/,        "ML-DSA-87",   s)
            gsub(/RSA +2048/,         "RSA-2048",    s)
            gsub(/ +/, " ", s)
            sub(/^ +/, "", s); sub(/ +$/, "", s)
            return s
        }
        # Match the wanted figure directly. The line also carries trailing
        # "N cycles / X Cycles/op" columns, and taking the last comma-separated
        # field would print those - they derive from the 24 MHz generic timer
        # rather than the core clock, so they are wrong by roughly 75x.
        /ops took/ && match($0, /[0-9.]+ ops\/sec/) {
            rate = substr($0, RSTART, RLENGTH); sub(/ ops\/sec/, "", rate)
            name = $0; sub(/ +[0-9]+ ops took.*/, "", name)
            printf "%s%s[A55]%s %-30s %12s ops/sec\n", R, C, R, shorten(name), rate
            fflush(); next
        }
        /iB took/ && match($0, /[0-9.]+ [KMG]iB\/s/) {
            rate = substr($0, RSTART, RLENGTH)
            name = $0; sub(/ +[0-9.]+ [KMG]iB took.*/, "", name)
            printf "%s%s[A55]%s %-30s %12s\n", R, C, R, shorten(name), rate
            fflush(); next
        }'
}

# wolfBoot on the M7 stamps the cycle counter into its status block at hal_init
# and again just before it hands off, so the difference is the cost of the
# whole verified boot on that core. Report it once it is available: the boot
# itself is milliseconds, which is why the log appears in one burst rather than
# scrolling.
m7_boot_time() {
    local out w2 w3 shown=0
    while [ "$shown" -eq 0 ]; do
        # memtool leads with a blank line, so match the address line itself
        # rather than trusting a line number.
        out=$("$MEMTOOL" r "$STATUS_ADDR" 4 2>/dev/null | awk -F: '/:/{print $2; exit}')
        w2=$(echo "$out" | awk '{print $3}')
        w3=$(echo "$out" | awk '{print $4}')
        if [ -n "$w2" ] && [ -n "$w3" ] && [ "$w3" != "00000000" ]; then
            printf '%s%s[M7 ]%s %s\n' "$R" "$C_M7" "$R" \
                "$(awk -v a="0x$w2" -v b="0x$w3" -v mhz="$M7_CORE_MHZ" \
                    'BEGIN { c = strtonum(b) - strtonum(a);
                             printf "wolfBoot verified boot: %.2f ms (%d cycles at %d MHz)",
                                    c / (mhz * 1000), c, mhz }')"
            shown=1
        fi
        sleep "$INTERVAL"
    done
}

m7_stream &
m7_pid=$!
m7_boot_time &
time_pid=$!
bench_stream &
bench_pid=$!

# Both halves run at once - the [M7 ] and [A55] prefixes say which core each
# line came from - and the demo ends when both have finished their single pass.
wait "$m7_pid" 2>/dev/null || true
wait "$bench_pid" 2>/dev/null || true
kill "$time_pid" 2>/dev/null || true

printf '%s\n' "$R"
echo "=================================================================="
echo " Demo complete. Both cores booted firmware verified with ML-DSA-87:"
echo "   Cortex-A55  wolfBoot -> Linux    (above, before this stream)"
echo "   Cortex-M7   wolfBoot -> Zephyr   [M7 ]"
echo " Power-cycle the board to run it again."
echo
# Printed here because the serial console is the only input at a show, and
# this is the last thing on it.
echo " Switch the boot chain, then power-cycle:"
echo "   sudo ~/switch-boot.sh 0      # stock U-Boot chain"
echo "   sudo ~/switch-boot.sh 1      # wolfBoot chain"
echo "   sudo ~/switch-boot.sh --show # which one is selected now"
echo "=================================================================="
