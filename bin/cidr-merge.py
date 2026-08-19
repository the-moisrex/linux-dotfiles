#!/usr/bin/env python3
#
# Example 1: All blocks in list.txt, one CIDR per line
#   cat list.txt | cidr.py
#
# Example 2: Echo CIDR blocks to stdout
#   echo 1.2.3.0/25 1.2.3.128/25 | cidr.py

import sys

try:
    from netaddr import IPNetwork, cidr_merge, AddrFormatError
except ImportError:
    sys.stderr.write(
        "Error: python3-netaddr is required. "
        "Install it (e.g. apt install python3-netaddr)\n"
    )
    sys.exit(1)

# Read from stdin
data = sys.stdin.readlines()
iplist = []

# fix the data
for index, line in enumerate(data):
    line = line.strip()
    if line:
        try:
            iplist.append(IPNetwork(line))
        except (AddrFormatError, ValueError, KeyError) as e:
            sys.stderr.write(f"Warning: skipping invalid entry on line {index + 1}: {line}\n")

data.clear()


# Create an IPSet of the CIDR blocks
# IPSet automatically runs cidr_merge
nets = cidr_merge(iplist)

# Output the superset of CIDR blocks
for cidr in nets:
    print(cidr)