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
                # Remove wildcards and leading dots
                domain = domain.replace('*', '').replace('www.', '').lstrip('.')

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

# Generate CRC32 hash array for O(log n) binary search
print("\n" + "="*60)
print("[STEP 2] Generating CRC32 hash array for fast lookup...")
print("="*60)

import zlib

# Calculate CRC32 for all domains
crc32_list = []
for domain in all_domains:
    # CRC32 returns signed int, convert to unsigned
    crc = zlib.crc32(domain.encode('utf-8')) & 0xFFFFFFFF
    crc32_list.append(crc)

# Sort for binary search
crc32_list.sort()

print(f"Generated {len(crc32_list):,} CRC32 hashes")

# Write C++ header file
src_dir = Path(__file__).parent.parent / 'src'
header_file = src_dir / 'blocklist_crc32.h'

print(f"Writing C++ header: {header_file}")

with open(header_file, 'w', encoding='utf-8') as f:
    f.write("// Auto-generated blocklist CRC32 hashes\n")
    f.write("// Generated by utils/gen_block_lists.py\n")
    f.write(f"// Total domains: {len(crc32_list):,}\n")
    f.write("// Use binary search for O(log n) lookup\n\n")
    f.write("#ifndef BLOCKLIST_CRC32_H\n")
    f.write("#define BLOCKLIST_CRC32_H\n\n")
    f.write("#include <Arduino.h>\n\n")
    f.write(f"const uint32_t BLOCKLIST_SIZE = {len(crc32_list)};\n\n")
    f.write("// Stored in Flash (PROGMEM) to save RAM\n")
    f.write("const uint32_t PROGMEM blocklist_crc32[] = {\n")

    # Write array in rows of 8 values for readability
    for i in range(0, len(crc32_list), 8):
        chunk = crc32_list[i:i+8]
        f.write("  ")
        f.write(", ".join(f"0x{crc:08X}U" for crc in chunk))
        if i + 8 < len(crc32_list):
            f.write(",\n")
        else:
            f.write("\n")

    f.write("};\n\n")
    f.write("#endif // BLOCKLIST_CRC32_H\n")

header_size = header_file.stat().st_size
print(f"[OK] Header file size: {header_size:,} bytes ({header_size / 1024:.1f} KB)")
print(f"[OK] Binary search depth: log2({len(crc32_list)}) = ~{len(crc32_list).bit_length()} comparisons")
print(f"[OK] Expected lookup time: ~{len(crc32_list).bit_length()}ms (vs 180-900ms currently)")

print("\n[Next step] Upload to NodeMCU")
print("   Run: pio run --target upload")
