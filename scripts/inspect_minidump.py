"""Read minidump directories and small diagnostic ranges without loading a full dump.

Usage: python scripts/inspect_minidump.py path/to/crash.dmp --output report.json
Uses only Python's standard library; capstone is optional for disassembly.
Addresses in stack_words/stack_hook_candidates are candidates, not an unwind.
"""
import argparse
import datetime
import hashlib
import json
import mmap
import struct
from pathlib import Path


def inspect_dump(path):
    with open(path, "rb") as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
        u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
        u64 = lambda offset: struct.unpack_from("<Q", data, offset)[0]
        if data[:4] != b"MDMP":
            raise ValueError("Not a Windows minidump")
        count, directory = u32(8), u32(12)
        streams = {u32(directory + i * 12): (u32(directory + i * 12 + 4), u32(directory + i * 12 + 8)) for i in range(count)}
        modules = []
        _, location = streams[4]
        for index in range(u32(location)):
            entry = location + 4 + index * 108
            name_rva = u32(entry + 20)
            name = data[name_rva + 4:name_rva + 4 + u32(name_rva)].decode("utf-16-le", errors="replace")
            version_ms, version_ls = u32(entry + 32), u32(entry + 36)
            modules.append({"base": u64(entry), "size": u32(entry + 8), "path": name,
                            "version": ".".join(str(part) for part in (version_ms >> 16, version_ms & 65535, version_ls >> 16, version_ls & 65535))})

        def owner(address):
            for module in modules:
                if module["base"] <= address < module["base"] + module["size"]:
                    return {"module": module["path"], "rva": hex(address - module["base"])}
            return None

        ranges = []
        if 9 in streams:
            _, location = streams[9]
            file_offset = u64(location + 8)
            for index in range(u64(location)):
                entry = location + 16 + index * 16
                start, size = u64(entry), u64(entry + 8)
                ranges.append((start, start + size, file_offset))
                file_offset += size
        elif 5 in streams:
            _, location = streams[5]
            for index in range(u32(location)):
                entry = location + 4 + index * 16
                start, size = u64(entry), u32(entry + 8)
                ranges.append((start, start + size, u32(entry + 12)))

        def read_virtual(address, size):
            for start, end, offset in ranges:
                if start <= address and address + size <= end:
                    return data[offset + address - start:offset + address - start + size]
            return b""

        report = {"dump": str(Path(path).resolve()), "file_size": len(data),
                  "timestamp_utc": datetime.datetime.fromtimestamp(u32(20), datetime.timezone.utc).isoformat(),
                  "flags": hex(u64(24)), "stream_types": sorted(streams), "memory_ranges": len(ranges)}
        if 6 in streams:
            _, location = streams[6]
            context = u32(location + 164)
            context_flags = u32(context)
            exception_address = u64(location + 24)
            report["exception"] = {"thread_id": u32(location), "code": hex(u32(location + 8)),
                                   "address": hex(exception_address), "owner": owner(exception_address),
                                   "parameters": [hex(u64(location + 40 + i * 8)) for i in range(min(u32(location + 32), 15))],
                                   "context_flags": hex(context_flags)}
            if context_flags & 0x10000:
                offsets = {"EDI": 156, "ESI": 160, "EBX": 164, "EDX": 168, "ECX": 172, "EAX": 176, "EBP": 180, "EIP": 184, "ESP": 196}
                registers = {name: u32(context + offset) for name, offset in offsets.items()}
                report["registers_x86"] = {name: hex(value) for name, value in registers.items()}
                code = read_virtual(registers["EIP"], 64)
                report["fault_bytes"] = code.hex()
                try:
                    import capstone
                    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
                    report["fault_disassembly"] = [{"address": hex(i.address), "instruction": i.mnemonic + " " + i.op_str} for i in decoder.disasm(code, registers["EIP"])]
                except ImportError:
                    pass
                stack = read_virtual(registers["ESP"], 128)
                report["stack_words"] = [{"address": hex(registers["ESP"] + offset), "value": hex(value), "owner": owner(value)}
                                         for offset, (value,) in zip(range(0, len(stack), 4), struct.iter_unpack("<I", stack))]
                frames = []
                bp = registers["EBP"]
                for _ in range(40):
                    frame = read_virtual(bp, 8)
                    if len(frame) != 8:
                        break
                    next_bp, return_address = struct.unpack("<II", frame)
                    frames.append({"ebp": hex(bp), "return_address": hex(return_address), "owner": owner(return_address), "next_ebp": hex(next_bp)})
                    if next_bp <= bp or next_bp - bp > 0x100000:
                        break
                    bp = next_bp
                report["frame_pointer_chain"] = frames

        candidates = []
        scanned_threads = []
        if 3 in streams:
            _, location = streams[3]
            for index in range(u32(location)):
                entry = location + 4 + index * 48
                thread_id = u32(entry)
                stack_start, size, offset = u64(entry + 24), u32(entry + 32), u32(entry + 36)
                size = min(size, 0x20000) & ~3
                # Full-memory dumps can set the thread stack's file RVA to
                # zero: its bytes are in Memory64List instead.
                stack = read_virtual(stack_start, size)
                if not stack and offset:
                    stack = data[offset:offset + size]
                scanned_threads.append({"thread_id": thread_id, "stack_start": hex(stack_start), "bytes_scanned": len(stack)})
                for word_index, (value,) in enumerate(struct.iter_unpack("<I", stack)):
                    module = owner(value)
                    if module and "jasshook.dll" in module["module"].lower():
                        candidates.append({"thread_id": thread_id, "stack_address": hex(stack_start + word_index * 4), "value": hex(value), "owner": module})
        report["stack_hook_candidates"] = candidates
        report["thread_stacks_scanned"] = scanned_threads
        report["modules"] = [{**module, "base": hex(module["base"]), "size": hex(module["size"])} for module in modules]
        report["limitations"] = ["Stack words are raw candidates; only frame_pointer_chain follows EBP links.",
                                 "A loaded module or a return-address candidate does not establish crash causation.",
                                 "Heap corruption can be detected later than the original invalid write."]
        return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump")
    parser.add_argument("--output")
    parser.add_argument("--compare-file", action="append", default=[])
    arguments = parser.parse_args()
    result = inspect_dump(arguments.dump)
    result["compared_files"] = []
    for filename in arguments.compare_file:
        digest = hashlib.sha256()
        with open(filename, "rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        result["compared_files"].append({"path": str(Path(filename).resolve()), "sha256": digest.hexdigest()})
    rendered = json.dumps(result, ensure_ascii=False, indent=2)
    if arguments.output:
        Path(arguments.output).write_text(rendered + "\n", encoding="utf-8")
        print(arguments.output)
    else:
        print(rendered)
