"""List translated functions that remain available to mod routine hooks."""
import argparse
from pathlib import Path
import re
import tomllib


def targets(mapping, profile, roots):
    translated = {int(value, 16) for value in re.findall(r'\{0x([0-9a-fA-F]+)u,\s*sub_', mapping.read_text(encoding='utf-8'))}
    config = tomllib.loads(profile.read_text(encoding='utf-8'))
    excluded = set(config.get('symbols', {}).values())
    for root in roots:
        for file in root.rglob('*'):
            if file.suffix in {'.cpp', '.inl', '.h'}:
                excluded.update(int(value, 16) for value in re.findall(r'X86_FUNC\(sub_([0-9A-Fa-f]{8})\)', file.read_text(encoding='utf-8')))
    return sorted(translated - excluded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mapping', required=True, type=Path)
    parser.add_argument('--profile', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--adapted', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    addresses = targets(args.mapping, args.profile, [args.source, args.adapted])
    text = '#pragma once\n#include <array>\n#include <cstdint>\nnamespace shaolin::mods {\n'
    text += f'inline constexpr std::array<uint32_t,{len(addresses)}> RoutineTargets{{\n'
    text += ''.join(f'0x{address:08X}u,\n' for address in addresses) + '};\n}\n'
    if not args.output.exists() or args.output.read_text(encoding='utf-8') != text:
        args.output.write_text(text, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
