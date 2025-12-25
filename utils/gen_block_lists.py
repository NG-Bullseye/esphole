#!/usr/bin/env python3
"""
Script for downloading and converting ad server lists to smaller files
Enhanced version with multiple sources and German optimization
"""

import os
import requests
from pathlib import Path

# Multiple block list sources
SOURCES = {
    'pgl_yoyo': {
        'url': 'https://pgl.yoyo.org/adservers/serverlist.php?hostformat=hosts&mimetype=plaintext&useip=0.0.0.0',
        'description': 'Peter Lowe\'s Ad and tracking server list'
    },
    'oisd_basic': {
        'url': 'https://small.oisd.nl/domainswild',
        'description': 'OISD Small (Optimized list)'
    },
    'stevenblack': {
        'url': 'https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts',
        'description': 'StevenBlack unified hosts'
    },
    'hagezi_light': {
        'url': 'https://raw.githubusercontent.com/hagezi/dns-blocklists/main/domains/light.txt',
        'description': 'Hagezi Light (includes EU/German ads)'
    }
}

# Use smaller lists for ESP8266 (limited LittleFS space)
# Comment out sources to reduce total domains
# Actual LittleFS size is ~1MB (not 3MB), so use conservative list
# OISD Small provides excellent coverage at ~50k domains
ENABLED_SOURCES = ['oisd_basic']

all_domains = set()

print("Downloading block lists from multiple sources...")
print(f"Active sources: {', '.join(ENABLED_SOURCES)}\n")

for source_name in ENABLED_SOURCES:
    if source_name not in SOURCES:
        continue

    source = SOURCES[source_name]
    print(f"[*] Downloading {source['description']}...")
    print(f"    URL: {source['url']}")

    try:
        response = requests.get(source['url'], timeout=30)
        response.raise_for_status()

        # Parse different formats
        count_before = len(all_domains)

        for line in response.text.splitlines():
            line = line.strip()

            # Skip comments and empty lines
            if not line or line.startswith('#') or line.startswith('!'):
                continue

            # Extract domain from various formats
            domain = None

            # Format: "0.0.0.0 domain.com" or "127.0.0.1 domain.com"
            if line.startswith('0.0.0.0') or line.startswith('127.0.0.1'):
                parts = line.split()
                if len(parts) >= 2:
                    domain = parts[1]

            # Format: "||domain.com^" (AdBlock format)
            elif line.startswith('||') and line.endswith('^'):
                domain = line[2:-1]

            # Format: "domain.com" (plain domain)
            elif '.' in line and ' ' not in line:
                domain = line

            # Clean and validate domain
            if domain:
                domain = domain.lower().strip()
                # Remove wildcards
                domain = domain.replace('*', '').replace('www.', '')

                # Basic validation (has dot, not too long, no special chars)
                if '.' in domain and len(domain) < 100 and domain.replace('.', '').replace('-', '').isalnum():
                    all_domains.add(domain)

        count_after = len(all_domains)
        print(f"    [+] Added {count_after - count_before} unique domains")

    except Exception as e:
        print(f"    [!] Error: {e}")

print(f"\n[OK] Total unique domains: {len(all_domains)}")

# Group domains by length for optimized lookup
print("\nGrouping domains by length...")
domain_files = {}
for domain in all_domains:
    length = len(domain)
    if length not in domain_files:
        domain_files[length] = []
    domain_files[length].append(domain)

# Create data directory
data_dir = Path(__file__).parent.parent / 'data'
data_dir.mkdir(exist_ok=True)

# Write files
print(f"\nWriting {len(domain_files)} host files...")
total_size = 0

for length in sorted(domain_files.keys()):
    domain_list = domain_files[length]
    filename = data_dir / f'hosts_{length}'

    with open(filename, 'w', encoding='utf-8') as f:
        for domain in sorted(domain_list):
            f.write(f',{domain}')
        f.write(',@@@\n')

    file_size = filename.stat().st_size
    total_size += file_size
    print(f"  hosts_{length:2d}: {len(domain_list):6d} domains ({file_size:8d} bytes)")

print(f"\n[Statistics]")
print(f"   Total files: {len(domain_files)}")
print(f"   Total domains: {len(all_domains):,}")
print(f"   Total size: {total_size:,} bytes ({total_size / 1024 / 1024:.2f} MB)")
print(f"   LittleFS available: ~3 MB")
print(f"   Usage: ~{(total_size / 1024 / 1024 / 3) * 100:.1f}%")

print("\n[DONE] Blocklists saved to data/")
print("\n[Next step] Upload to NodeMCU")
print("   Run: pio run --target uploadfs")
