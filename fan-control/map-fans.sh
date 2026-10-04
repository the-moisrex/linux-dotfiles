#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../setup/lib/common.sh
source "$SCRIPT_DIR/../setup/lib/common.sh"
SHOW_HELP=false
parse_common_flags "$@"

if [[ "$SHOW_HELP" == "true" ]]; then
  cat <<'USAGE'
Usage: ./fan-control/map-fans.sh [flags...]

Map PWM channels to fan tachometers on a Nuvoton Super-I/O chip (nct6775 family).
For every pwmN it sets manual mode, sweeps duty 255 -> 0, and reports which
fanN_input reacts. Run as root: sudo ./fan-control/map-fans.sh

WARNING: fans are briefly spun to max and briefly stopped during the sweep.

Options:
  --verbose    Show extra debug output.
  -h, --help   Show this help message.
USAGE
    exit 0
fi

log "Mapping PWM channels to fan tachometers"

if [[ $EUID -ne 0 ]]; then
    die "Run as root: sudo $0"
fi

CHIP=""
for d in /sys/class/hwmon/hwmon*; do
    [[ -r "$d/name" ]] || continue
    if [[ "$(cat "$d/name")" == nct* ]]; then
        CHIP="$d"
        break
    fi
done

if [[ -z "$CHIP" ]]; then
    die "No nct* hwmon chip found (try: sudo modprobe nct6775)"
fi

log_step "Chip: $(basename "$CHIP") ($(cat "$CHIP/name"))"

mapfile -t PWMS < <(compgen -G "$CHIP/pwm*" | sed 's|.*/||' | grep -E '^pwm[0-9]+$' | sort -V | sed "s|^|$CHIP/|")
mapfile -t FANS < <(compgen -G "$CHIP/fan*_input" | sed 's|.*/||' | grep -E '^fan[0-9]+_input$' | sort -V | sed "s|^|$CHIP/|")

if [[ ${#PWMS[@]} -eq 0 || ${#FANS[@]} -eq 0 ]]; then
    die "No pwm/fan attributes under $CHIP"
fi

read_fans() {
    local out="" f
    for f in "${FANS[@]}"; do
        out+="$(basename "$f" _input)=$(cat "$f") "
    done
    printf '%s' "$out"
}

# fancontrol fights over the channels it owns; stop it and bring it back
FC_WAS_ACTIVE=false
if systemctl is-active --quiet fancontrol.service 2>/dev/null; then
    FC_WAS_ACTIVE=true
    log_step "Stopping fancontrol.service for the sweep"
    systemctl stop fancontrol.service
fi
restart_fancontrol() {
    if [[ "$FC_WAS_ACTIVE" == "true" ]]; then
        systemctl start fancontrol.service || warn "Could not restart fancontrol"
    fi
}
trap restart_fancontrol EXIT

# tach readings fluctuate a lot on some headers, so take min/max over several
# samples at each duty instead of trusting a single reading
combine_samples() { # $1=previous $2=new $3=min|max
    paste <(printf '%s\n' "$1") <(printf '%s\n' "$2") | awk -v want="$3" '
        { for (i = 1; i <= NF; i++) {
            split($i, a, "=")
            if (!(a[1] in m)) m[a[1]] = a[2]
            else if (want == "min" && a[2] < m[a[1]]) m[a[1]] = a[2]
            else if (want == "max" && a[2] > m[a[1]]) m[a[1]] = a[2]
        } }
        END { for (k in m) printf "%s=%s ", k, m[k] }'
}

warn "Sweeping ${#PWMS[@]} channels; fans will speed up and stop momentarily"

declare -A HI LO
for p in "${PWMS[@]}"; do
    name=$(basename "$p")
    orig=$(cat "$p")
    orig_en=$(cat "${p}_enable" 2>/dev/null || echo "")

    echo 1 > "$p"_enable
    echo 255 > "$p"
    sleep 4
    hi=""
    for _ in 1 2 3; do
        hi=$(combine_samples "$hi" "$(read_fans)" min)
        sleep 1
    done
    HI[$name]="$hi"
    echo 0 > "$p"
    sleep 4
    lo=""
    for _ in 1 2 3; do
        lo=$(combine_samples "$lo" "$(read_fans)" max)
        sleep 1
    done
    LO[$name]="$lo"

    echo "$orig" > "$p"
    [[ -n "$orig_en" ]] && echo "$orig_en" > "$p"_enable
    log_verbose "$name hi-min[${HI[$name]}] lo-max[${LO[$name]}]"
done

log "Results (min RPM at duty 255 vs max RPM at duty 0):"
printf '  %-7s %s\n' "channel" "fans (hi -> lo)"
for p in "${PWMS[@]}"; do
    name=$(basename "$p")
    printf '  %-7s %s\n' "$name" "${HI[$name]} -> ${LO[$name]}"
done

log "Mapping:"
found=false
for p in "${PWMS[@]}"; do
    name=$(basename "$p")
    for f in "${FANS[@]}"; do
        fan=$(basename "$f" _input)
        hi=$(echo "${HI[$name]}" | tr ' ' '\n' | sed -n "s/^${fan}=\\([0-9]*\\)$/\\1/p")
        lo=$(echo "${LO[$name]}" | tr ' ' '\n' | sed -n "s/^${fan}=\\([0-9]*\\)$/\\1/p")
        [[ -z "$hi" || -z "$lo" ]] && continue
        if (( lo == 0 && hi > 0 )); then
            log_step "$name controls $fan (stops at duty 0)"
            found=true
        elif (( hi - lo > 150 )); then
            log_step "$name influences $fan ($hi -> $lo)"
            found=true
        fi
    done
done

if [[ "$found" == "false" ]]; then
    warn "No channel responded. Fans without a reacting tach are either"
    warn "hard-wired (e.g. AIO tach on a passthrough) or on a header this"
    warn "chip cannot drive. Check BIOS Q-Fan for those."
fi

log "Done"
