#!/bin/sh
# Opt-in, recorded seeding of an opencode profile from host files, then exec.
#
# The decision is stored in /profile/.profile so a profile's state never
# depends on which flags happened to run first:
#   seeded=yes|no        host config has (not) been applied
#   seeded_auth=...      host auth.json has (not) been copied
#   seeded_config=...    host opencode.jsonc has (not) been copied
#   seeded_at=<utc ts>
#
# OC_SEED=1   fill in what the marker says is missing (never clobbers)
# OC_RESEED=1 overwrite auth/config from the host (sessions untouched)
set -eu

config_dir="${XDG_CONFIG_HOME:-$HOME/.config}/opencode"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/opencode"
profile_dir="$(dirname "${XDG_CONFIG_HOME:-$HOME/.config}")"
marker="$profile_dir/.profile"

log() { printf '[oc] %s\n' "$*" >&2; }

write_marker() { # $1=seeded $2=seeded_auth $3=seeded_config
    tmp="$marker.tmp.$$"
    {
        printf 'seeded=%s\n' "$1"
        printf 'seeded_auth=%s\n' "$2"
        printf 'seeded_config=%s\n' "$3"
        printf 'seeded_at=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } >"$tmp"
    mv -f "$tmp" "$marker"
}

host_has_seed_sources() {
    [ -f /seed/auth.json ] || [ -f /seed/config/opencode.jsonc ]
}

# Missing config counts as trivial (i.e. it should be replaced by a seed).
config_is_trivial() {
    [ -f "$config_dir/opencode.jsonc" ] || return 0
    [ "$(wc -c <"$config_dir/opencode.jsonc")" -lt 200 ]
}

# $1=force(0|1); prints yes|no
seed_auth() {
    if [ -f /seed/auth.json ]; then
        if [ ! -f "$data_dir/auth.json" ] || [ "$1" = "1" ]; then
            cp -a /seed/auth.json "$data_dir/auth.json"
        fi
    fi
    if [ -f "$data_dir/auth.json" ]; then echo yes; else echo no; fi
}

# $1=force(0|1); copies host config, rewrites loopback URLs on fresh copies,
# prints yes|no
seed_config() {
    copied=0
    if [ -d /seed/config ]; then
        if [ "$1" = "1" ] || config_is_trivial; then
            if [ -f /seed/config/opencode.jsonc ]; then
                mkdir -p "$config_dir"
                cp -a /seed/config/opencode.jsonc "$config_dir/opencode.jsonc"
                copied=1
            fi
        fi
        for f in opencode.json package.json package-lock.json; do
            if [ -f "/seed/config/$f" ] && { [ "$1" = "1" ] || [ ! -e "$config_dir/$f" ]; }; then
                mkdir -p "$config_dir"
                cp -a "/seed/config/$f" "$config_dir/$f"
            fi
        done
        if [ -d /seed/config/themes ] && { [ "$1" = "1" ] || [ ! -e "$config_dir/themes" ]; }; then
            mkdir -p "$config_dir"
            rm -rf "$config_dir/themes"
            cp -a /seed/config/themes "$config_dir/themes"
        fi
    fi

    # Inside a container localhost is the container itself; host services are
    # reached through host.containers.internal. Only rewrite what we just copied.
    if [ "$copied" = "1" ]; then
        sed -i -e 's#http://localhost:#http://host.containers.internal:#g' \
               -e 's#https://localhost:#https://host.containers.internal:#g' \
               -e 's#http://127.0.0.1:#http://host.containers.internal:#g' \
               -e 's#https://127.0.0.1:#https://host.containers.internal:#g' \
               "$config_dir/opencode.jsonc" 2>/dev/null || true
    fi

    if [ -f "$config_dir/opencode.jsonc" ] && ! config_is_trivial; then
        echo yes
    else
        echo no
    fi
}

mkdir -p "$config_dir" "$data_dir" \
    "${XDG_CACHE_HOME:-$HOME/.cache}" "${XDG_STATE_HOME:-$HOME/.local/state}"

seed_req="${OC_SEED:-0}"
reseed_req="${OC_RESEED:-0}"

if [ "$reseed_req" = "1" ]; then
    if host_has_seed_sources; then
        a="$(seed_auth 1)"
        c="$(seed_config 1)"
        write_marker yes "$a" "$c"
        log "profile reseeded from host (sessions kept)"
    else
        log "reseed requested but no /seed sources mounted; profile unchanged"
        [ -f "$marker" ] || write_marker no no no
    fi
elif [ "$seed_req" = "1" ]; then
    if host_has_seed_sources; then
        a="$(seed_auth 0)"
        c="$(seed_config 0)"
        write_marker yes "$a" "$c"
        log "profile seeded from host (auth=$a config=$c)"
    else
        log "seed requested but no /seed sources mounted; profile left unseeded"
        [ -f "$marker" ] || write_marker no no no
    fi
elif [ ! -f "$marker" ]; then
    # First start without seeding (or a profile from before markers existed):
    # record what is actually there, never seed implicitly.
    a=no
    c=no
    [ -f "$data_dir/auth.json" ] && a=yes
    if [ -f "$config_dir/opencode.jsonc" ] && ! config_is_trivial; then
        c=yes
    fi
    if [ "$a" = "yes" ] || [ "$c" = "yes" ]; then
        write_marker yes "$a" "$c"
        log "existing profile recorded (seeded=yes auth=$a config=$c)"
    else
        write_marker no no no
        log "profile initialized unseeded (use --seed to seed it from the host)"
    fi
fi

exec "$@"
