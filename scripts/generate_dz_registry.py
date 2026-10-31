#!/usr/bin/env python3
"""Extract Dz native registrations from an x86 War3Plugin.dll without loading it.

Dependencies: Python 3.10+, pefile, capstone (python -m pip install pefile capstone).
Run with --check to verify the checked-in registry and binary identity without
changing files. Run without --check after a plugin update, then rebuild the DLL.
Both direct fastcall registrations and stack-initialized registration tables
are decoded; offsets are never copied from a previous plugin version.
"""

from __future__ import annotations

import argparse
from collections import Counter, deque
from dataclasses import dataclass
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import sys

try:
    import capstone
    import pefile
except ImportError as exc:
    raise SystemExit("Install the generator dependencies: python -m pip install pefile capstone") from exc


NAME = re.compile(r"(?:Dz|RequestExtra)[A-Za-z0-9_]+\Z")
ARG = r"(?:[IRSBC]|H[A-Za-z0-9_]+;)"
SIGNATURE = re.compile(r"\(" + ARG + r"*\)(?:[IRSBVC]|H[A-Za-z0-9_]+;)\Z")
STORE = re.compile(
    r"dword ptr \[(ebp|esp)(?: ([+-]) (0x[0-9a-f]+|[0-9]+))?\], (0x[0-9a-f]+|[0-9]+)\Z"
)
HOOK_NAMES = (
    "DzAPI_Map_GetMapLevel", "DzAPI_Map_HasMallItem",
    "DzAPI_Map_SaveServerValue", "DzAPI_Map_GetServerValue", "DzAPI_Map_Stat_SetStat",
)


@dataclass(frozen=True)
class Native:
    name: str
    signature: str
    rva: int


class Plugin:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        self.pe = pefile.PE(data=self.data)
        if self.pe.FILE_HEADER.Machine != 0x14C or self.pe.OPTIONAL_HEADER.Magic != 0x10B:
            raise ValueError("Expected a PE32 x86 plugin, not a 64-bit binary")
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.size = self.pe.OPTIONAL_HEADER.SizeOfImage
        self.executable = [section for section in self.pe.sections if section.Characteristics & 0x20000000]
        self.text = next((s for s in self.executable if s.Name.rstrip(b"\0") == b".text"), None)
        if self.text is None:
            raise ValueError("Plugin has no executable .text section")

    def executable_va(self, va: int) -> bool:
        rva = va - self.base
        return any(s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize for s in self.executable)

    @lru_cache(maxsize=16384)
    def string(self, va: int | None) -> str | None:
        if va is None or not self.base <= va < self.base + self.size:
            return None
        data = self.pe.get_data(va - self.base, 256)
        end = data.find(b"\0")
        if end <= 0 or not all(32 <= c < 127 for c in data[:end]):
            return None
        return data[:end].decode("ascii")

    def native(self, handler: int | None, name_va: int | None, sig_va: int | None) -> Native | None:
        name, sig = self.string(name_va), self.string(sig_va)
        if name is None or sig is None or not NAME.fullmatch(name) or not SIGNATURE.fullmatch(sig):
            return None
        if handler is None or not self.executable_va(handler):
            raise ValueError(f"Native {name} points outside executable sections")
        return Native(name, sig, handler - self.base)

    def extract(self) -> tuple[list[Native], int, int, int]:
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        decoder.skipdata = True  # Decode beyond alignment/data embedded in .text.
        direct: list[tuple[int, Native]] = []
        tables: list[Native] = []
        for section in self.executable:
            history: deque[tuple[str, str]] = deque(maxlen=20)
            stores: deque[tuple[str, int, int]] = deque(maxlen=3)
            code = section.get_data()[:section.Misc_VirtualSize]
            for _address, _size, mnemonic, operand in decoder.disasm_lite(code, self.base + section.VirtualAddress):
                if mnemonic == "call" and re.fullmatch(r"0x[0-9a-f]+", operand):
                    handler = name_va = sig_va = None
                    seen: set[str] = set()
                    for prev_mnemonic, prev_operand in reversed(history):
                        if prev_mnemonic in ("call", "ret", "retf", "int3") or prev_mnemonic.startswith("j"):
                            break
                        for register in ("ecx", "edx"):
                            if register not in seen and prev_operand.startswith(register + ","):
                                seen.add(register)
                                match = re.fullmatch(register + r", (0x[0-9a-f]+)", prev_operand)
                                value = int(match.group(1), 16) if match and prev_mnemonic == "mov" else None
                                if register == "ecx":
                                    handler = value
                                else:
                                    name_va = value
                        if prev_mnemonic == "push" and "stack" not in seen:
                            seen.add("stack")
                            if re.fullmatch(r"0x[0-9a-f]+", prev_operand):
                                sig_va = int(prev_operand, 16)
                    native = self.native(handler, name_va, sig_va)
                    if native:
                        target = int(operand, 16)
                        if not self.executable_va(target):
                            raise ValueError("Registrar is outside executable sections")
                        direct.append((target, native))

                match = STORE.fullmatch(operand) if mnemonic == "mov" else None
                if match:
                    register, sign, displacement, immediate = match.groups()
                    offset = int(displacement, 0) if displacement else 0
                    if sign == "-":
                        offset = -offset
                    stores.append((register, offset, int(immediate, 0)))
                    if len(stores) == 3:
                        first, second, third = stores
                        if first[0] == second[0] == third[0] and second[1] == first[1] + 4 and third[1] == first[1] + 8:
                            native = self.native(first[2], second[2], third[2])
                            if native:
                                tables.append(native)
                else:
                    stores.clear()
                history.append((mnemonic, operand))

        targets = Counter(target for target, _native in direct)
        if len(targets) != 1 or not tables:
            raise ValueError(f"Unrecognized registration layout: direct targets={dict(targets)}, tables={len(tables)}")
        records: dict[str, Native] = {}
        for native in [record for _target, record in direct] + tables:
            old = records.setdefault(native.name, native)
            if old != native:
                raise ValueError(f"Conflicting registrations for {native.name}: {old} versus {native}")
        missing = set(HOOK_NAMES) - records.keys()
        if missing:
            raise ValueError("Required hook registrations are missing: " + ", ".join(sorted(missing)))
        return sorted(records.values(), key=lambda record: (record.rva, record.name)), next(iter(targets)) - self.base, len(direct), len(tables)

    def header(self, natives: list[Native], registrar_rva: int, direct: int, tables: int) -> str:
        text_data = self.text.get_data()[:self.text.Misc_VirtualSize]
        lines = [
            "// Auto-generated by scripts/generate_dz_registry.py from War3Plugin.dll. Do not edit.",
            f"// Verified PE32 registrations: {direct} direct + {tables} table = {len(natives)} unique natives.",
            "// Text SHA-256 covers .text VirtualSize bytes on disk, before relocations/hooks.",
            "#pragma once",
            f"static constexpr unsigned kDzPluginTimeDateStamp = 0x{self.pe.FILE_HEADER.TimeDateStamp:08x};",
            f"static constexpr unsigned kDzPluginSizeOfImage = 0x{self.size:x};",
            f"static constexpr unsigned kDzPluginCheckSum = 0x{self.pe.OPTIONAL_HEADER.CheckSum:x};",
            f"static constexpr unsigned kDzPluginTextRva = 0x{self.text.VirtualAddress:x};",
            f"static constexpr unsigned kDzPluginTextSize = 0x{self.text.Misc_VirtualSize:x};",
            f"static constexpr unsigned kDzPluginRegistrarRva = 0x{registrar_rva:x};",
            f'static constexpr char kDzPluginSha256[] = "{hashlib.sha256(self.data).hexdigest()}";',
            f'static constexpr char kDzPluginTextSha256[] = "{hashlib.sha256(text_data).hexdigest()}";',
            "struct DzEntry { const char* name; const char* sig; unsigned rva; };",
            "static const DzEntry g_dzTable[] = {",
        ]
        lines += [f"    {{{json.dumps(native.name)}, {json.dumps(native.signature)}, 0x{native.rva:x}}}," for native in natives]
        lines += ["};", "static const int g_dzTableCount = (int)(sizeof(g_dzTable) / sizeof(g_dzTable[0]));", ""]
        return "\n".join(lines)

    def report_hooks(self, natives: list[Native]) -> None:
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        for native in natives:
            if native.name not in HOOK_NAMES:
                continue
            data = self.pe.get_data(native.rva, 32)
            span = 0
            instructions = []
            for _address, size, mnemonic, operand in decoder.disasm_lite(data, self.base + native.rva):
                if span >= 5:
                    break
                span += size
                instructions.append(mnemonic + " " + operand)
            print(f"{native.name} RVA=0x{native.rva:x} minimum hook span={span}: {'; '.join(instructions)}")
            print("  first 32 bytes: " + data.hex(" "))


def main() -> int:
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", type=Path, default=project.parent / "kkduizhan/plugins/Game/War3Plugin.dll")
    parser.add_argument("--output", type=Path, default=project / "src/dz_registry.h")
    parser.add_argument("--check", action="store_true", help="Check exact registry, PE identity and hashes; do not write")
    parser.add_argument("--report-hooks", action="store_true", help="Print hook entry bytes and whole-instruction spans")
    args = parser.parse_args()
    try:
        plugin = Plugin(args.plugin)
        natives, registrar, direct, tables = plugin.extract()
        header = plugin.header(natives, registrar, direct, tables)
        if args.check:
            if not args.output.is_file() or args.output.read_text(encoding="utf-8") != header:
                print("Registry or plugin identity mismatch. Regenerate dz_registry.h and rebuild JassHook.dll.", file=sys.stderr)
                return 1
            print(f"Verified registry, PE identity and SHA-256 hashes: {len(natives)} natives")
        else:
            args.output.write_text(header, encoding="utf-8", newline="\n")
            print(f"Wrote {args.output}: {len(natives)} natives ({direct} direct + {tables} table)")
        if args.report_hooks:
            plugin.report_hooks(natives)
        return 0
    except (OSError, ValueError, pefile.PEFormatError) as exc:
        print(f"Registry generation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
