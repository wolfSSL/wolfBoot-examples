#!/usr/bin/env bash
# Left-pane demo loop: live wolfCrypt PQC benchmarks on the i.MX95 A55 cluster.
#
# Environment:
#   MODE=auto|baseline|sha3-crypto|ab   which wolfSSL build to run (default auto)
#   LOOP=1|0                            run continuously (default 1)
#   PAUSE=<seconds>                     pause between cycles (default 5)
#   ONCE_ARGS="..."                     override the algorithm set

set -uo pipefail

WOLF_ROOT=/opt/wolfssl
# Named at runtime rather than baked in: the image is plain aarch64 and runs on
# any Arm64 Torizon module, so naming one SoC in the banner would be wrong on
# the others. A container does not normally see /proc/device-tree, so the model
# is only available when the host exposes it; brace the redirect so a missing
# file is silent rather than a shell error on stderr.
soc_model() { { tr -d '\0' < /proc/device-tree/model; } 2>/dev/null; }
SOC_LABEL="${SOC_LABEL:-$(soc_model)}"
SOC_LABEL="${SOC_LABEL:-Arm64 module}"
MODE="${MODE:-auto}"
LOOP="${LOOP:-1}"
PAUSE="${PAUSE:-5}"

B=$'\033[1m'; DIM=$'\033[2m'; CYAN=$'\033[36m'; GREEN=$'\033[32m'
YELLOW=$'\033[33m'; RED=$'\033[31m'; R=$'\033[0m'
[ -t 1 ] || { B=""; DIM=""; CYAN=""; GREEN=""; YELLOW=""; RED=""; R=""; }

have_sha3() { grep -m1 '^Features' /proc/cpuinfo 2>/dev/null | grep -qw sha3; }

select_build() {
    case "${MODE}" in
        baseline|sha3-crypto) echo "${MODE}" ;;
        auto)
            # FEAT_SHA3 is optional on Cortex-A55. Running the sha3-crypto
            # binary without it faults, so this check is not cosmetic.
            if have_sha3; then echo sha3-crypto; else echo baseline; fi ;;
        *) echo baseline ;;
    esac
}

banner() {
    local build="$1" feats ver
    feats="$(grep -m1 '^Features' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //')"
    ver="$("${WOLF_ROOT}/${build}/bin/benchmark" -? 2>&1 | grep -m1 -o 'wolfSSL version [0-9.]*')"

    echo "${CYAN}${B}"
    echo "  wolfSSL / wolfCrypt post-quantum benchmarks"
    echo "  ${SOC_LABEL}  --  Torizon OS"
    echo "${R}${DIM}  ------------------------------------------------------------${R}"
    printf "  %-14s %s\n" "Library:"  "${ver:-unknown}"
    printf "  %-14s %s\n" "Build:"    "${build}"
    printf "  %-14s %s\n" "Cores:"    "$(nproc)"
    printf "  %-14s %s\n" "Kernel:"   "$(uname -r)"
    printf "  %-14s %s\n" "CPU flags:" "${feats:-unavailable}"

    if [ "${build}" = "sha3-crypto" ]; then
        printf "  %-14s ${GREEN}%s${R}\n" "Keccak:" \
            "ARMv8.2 SHA3 instructions (EOR3/RAX1/XAR/BCAX)"
    elif have_sha3; then
        printf "  %-14s ${YELLOW}%s${R}\n" "Keccak:" \
            "NEON only -- CPU has FEAT_SHA3, build does not use it"
    else
        printf "  %-14s ${YELLOW}%s${R}\n" "Keccak:" \
            "NEON only -- this CPU has no FEAT_SHA3"
    fi
    # ML-DSA has no ARM assembly in wolfSSL, so its numbers ride entirely on
    # the Keccak backend named above. Say so, rather than let a reader assume
    # a hand-tuned ML-DSA path exists.
    printf "  ${DIM}%s${R}\n" "ML-KEM: NEON NTT + 3-way NEON Keccak. ML-DSA: no ARM asm; Keccak-bound."
    echo "${DIM}  ------------------------------------------------------------${R}"
    echo
}

run_cycle() {
    local build="$1" bin="${WOLF_ROOT}/$1/bin/benchmark"
    if [ ! -x "${bin}" ]; then
        echo "${RED}missing benchmark binary: ${bin}${R}" >&2
        return 1
    fi
    if [ -n "${ONCE_ARGS:-}" ]; then
        # shellcheck disable=SC2086
        "${bin}" ${ONCE_ARGS}
    else
        "${bin}" -ml-kem-512 -ml-kem-768 -ml-kem-1024 \
                 -ml-dsa-44 -ml-dsa-65 -ml-dsa-87 \
                 -ecc -rsa -sha3-256 -shake256
    fi
}

main() {
    local build cycle=0 status=
    build="$(select_build)"

    if [ "${MODE}" = auto ] && [ "${build}" = baseline ] && have_sha3; then
        : # unreachable, kept for clarity
    fi
    if [ "${MODE}" = sha3-crypto ] && ! have_sha3; then
        echo "${RED}WARNING: MODE=sha3-crypto but this CPU does not advertise FEAT_SHA3." >&2
        echo "         The binary is likely to fault with SIGILL.${R}" >&2
    fi

    while :; do
        cycle=$((cycle + 1))
        if [ "${MODE}" = ab ]; then
            # Alternate builds so the SHA-3 lever is visible live, side by side
            # across cycles, without restarting the container.
            if [ $((cycle % 2)) -eq 1 ]; then build=baseline
            elif have_sha3;                 then build=sha3-crypto
            else                                 build=baseline
            fi
        fi

        banner "${build}"
        printf "  ${GREEN}${B}[RUNNING]${R} benchmark cycle %d, started %s\n\n" \
            "${cycle}" "$(date -u '+%H:%M:%SZ')"

        if run_cycle "${build}"; then
            status="${GREEN}${B}[OK]${R}"
        else
            status="${RED}${B}[FAILED]${R}"
        fi

        [ "${LOOP}" = "1" ] || break
        echo
        printf "  %b cycle %d complete, next run in %ss\n" \
            "${status}" "${cycle}" "${PAUSE}"
        sleep "${PAUSE}"
        echo
    done
}

main "$@"
