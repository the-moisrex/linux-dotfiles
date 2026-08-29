#!/bin/bash
DIR="$(cd "$(dirname "$0")/../tshark" && pwd)"
exec tshark -q -Y dns -X lua_script:"$DIR/dns.lua" "$@"
