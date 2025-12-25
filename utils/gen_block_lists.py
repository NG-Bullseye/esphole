#!/usr/bin/env python3
"""
Script for downloading and converting ad server lists to smaller files
"""

import os
import requests
from pathlib import Path

HOSTS_URL = "https://pgl.yoyo.org/adservers/serverlist.php?hostformat=hosts&mimetype=plaintext&useip=0.0.0.0"

print("Downloading blocklist...")
response = requests.get(HOSTS_URL)
response.raise_for_status()

# Filter out comments and extract domains
domains = []
for line in response.text.splitlines():
    if line.startswith('#') or not line.strip():
        continue
    if '0.0.0.0' in line:
        domain = line.replace('0.0.0.0', '').strip()
        if domain:
            domains.append(domain)

print(f"Found {len(domains)} domains to block")

# Group domains by length and write to files
domain_files = {}
for domain in domains:
    domain_clean = domain.strip().replace('\n', ' ').replace('\r', ' ')
    length = len(domain_clean)
    if length not in domain_files:
        domain_files[length] = []
    domain_files[length].append(domain_clean)

# Create data directory
data_dir = Path(__file__).parent.parent / 'data'
data_dir.mkdir(exist_ok=True)

# Write files
print(f"Writing {len(domain_files)} host files...")
for length, domain_list in domain_files.items():
    filename = data_dir / f'hosts_{length}'
    with open(filename, 'w', encoding='utf-8') as f:
        for domain in domain_list:
            f.write(f',{domain}')
        f.write(',@@@\n')
    print(f"  hosts_{length}: {len(domain_list)} domains")

print("Done! Blocklists saved to data/")
