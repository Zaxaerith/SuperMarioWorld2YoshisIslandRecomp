#!/usr/bin/env python3
"""
parse_labels.py
Extracts labels, routine offsets, variables, and comments from Yoshi's Island disassembly.
"""

import os
import re
import sys

def parse_asm_file(filepath):
    labels = {}
    pattern = re.compile(r'^([a-zA-Z0-9_]+):\s*(?:;\s*\$([0-9A-Fa-f]{6}))?')
    equ_pattern = re.compile(r'^!([a-zA-Z0-9_]+)\s*=\s*\$([0-9A-Fa-f]+)')

    with open(filepath, 'r', encoding='latin-1') as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            m = pattern.match(line)
            if m:
                label_name = m.group(1)
                addr = m.group(2)
                labels[label_name] = {'addr': addr, 'line': line_no, 'file': os.path.basename(filepath)}
            m_equ = equ_pattern.match(line)
            if m_equ:
                var_name = m_equ.group(1)
                val = m_equ.group(2)
                labels[f"!{var_name}"] = {'val': val, 'line': line_no, 'file': os.path.basename(filepath)}
    return labels

def main():
    disasm_dir = os.path.join(os.path.dirname(__file__), '..', 'yoshisisland-disassembly-master', 'disassembly')
    if not os.path.exists(disasm_dir):
        print(f"Error: disassembly directory not found at {disasm_dir}")
        return

    total_labels = 0
    for root, _, files in os.walk(disasm_dir):
        for f in files:
            if f.endswith('.asm'):
                path = os.path.join(root, f)
                lbls = parse_asm_file(path)
                total_labels += len(lbls)
                print(f"Parsed {f:20s}: {len(lbls)} labels/variables")

    print(f"\nTotal parsed symbols: {total_labels}")

if __name__ == '__main__':
    main()
