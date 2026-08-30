#!/bin/bash

show_help() {
    cat <<'EOF'
Usage: dns.sh [tshark-options...]

Capture and analyze DNS traffic using tshark with a custom Lua script.

This script runs tshark with DNS display filter and a custom Lua script
for enhanced DNS packet analysis.

All arguments are passed directly to tshark.

Options:
  -h, --help    Show this help message (also passes to tshark)
EOF
}

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    show_help
    exit 0
fi

DIR="$(cd "$(dirname "$0")/../tshark" && pwd)"
exec tshark -q -Y dns -X lua_script:"$DIR/dns.lua" "$@"
