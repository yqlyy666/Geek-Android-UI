"""Make a work-only AMD64 UI test copy; never launches or alters production.

The embedded manifest requests asInvoker with uiAccess=false. An optional
test-specific singleton mutex/event keeps an existing native window open.
Only the requested manifest text, optional two singleton names and PE checksum
may differ. This is a reduced-privilege inspection copy, not an installer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
WORK = ROOT.parent.resolve()
import pefile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def in_work(path):
    resolved = path.resolve()
    if not resolved.is_relative_to(WORK):
        raise ValueError(f"Test output must stay inside {WORK}")
    return resolved


def manifest_resource(pe):
    found = []
    for resource_type in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if resource_type.id != 24:
            continue
        for name in resource_type.directory.entries:
            if name.id != 1:
                continue
            for language in name.directory.entries:
                data = language.data.struct
                found.append((pe.get_offset_from_rva(data.OffsetToData), data.Size,
                              language.id))
    if len(found) != 1:
        raise ValueError(f"Expected one RT_MANIFEST/1 resource, found {len(found)}")
    return found[0]


def make(source, output, isolate_instance=False, instance_tag=None):
    source = source.resolve()
    output = in_work(output)
    if source == output:
        raise ValueError("The source payload cannot be overwritten")
    if output.exists():
        raise ValueError(f"Refusing to overwrite {output}")
    report_path = output.with_suffix(".test-copy.json")
    if report_path.exists():
        raise ValueError(f"Refusing to overwrite report {report_path}")
    original = source.read_bytes()
    image = bytearray(original)
    pe = pefile.PE(data=original)
    if pe.FILE_HEADER.Machine != 0x8664:
        raise ValueError("Use the bare AMD64 payload, not the x86 desktop launcher")
    offset, size, language = manifest_resource(pe)
    manifest = bytes(image[offset:offset + size])
    token = b'level="requireAdministrator"'
    replacement = b'level="asInvoker"'
    if manifest.count(token) != 1:
        raise ValueError("Expected one requireAdministrator attribute in the manifest")
    at = offset + manifest.index(token)
    padded = replacement + b" " * (len(token) - len(replacement))
    image[at:at + len(token)] = padded
    xml = ET.fromstring(bytes(image[offset:offset + size]).decode("utf-8-sig"))
    execution = [e for e in xml.iter() if e.tag.endswith("requestedExecutionLevel")]
    if len(execution) != 1 or execution[0].attrib != {"level": "asInvoker", "uiAccess": "false"}:
        raise ValueError("Manifest downgrade did not produce asInvoker/uiAccess=false")
    changes = [{"kind": "manifest", "offset": at, "length": len(token),
                "before": token.decode(), "after": padded.decode()}]

    tag = None
    if isolate_instance:
        tag = instance_tag or "GeekUI" + digest(original)[:9]
        if not re.fullmatch(r"[A-Za-z0-9_]{15}", tag) or tag == "GeekUninstaller":
            raise ValueError("The test instance tag must contain exactly 15 ASCII letters/digits/underscores")
        # These are the two names passed from native startup to its existing
        # SetMutexName and SetEventName methods. Preserve UTF-16 lengths and
        # all CreateMutex/OpenMutex/CreateEvent/activation logic unchanged.
        for raw_offset, old_name, new_name in (
                (0x2576C0, "Local\\GeekUninstallerMutex", "Local\\" + tag + "Mutex"),
                (0x2576F8, "Local\\GeekUninstaller", "Local\\" + tag)):
            old = (old_name + "\0").encode("utf-16le")
            new = (new_name + "\0").encode("utf-16le")
            if len(old) != len(new) or image[raw_offset:raw_offset + len(old)] != old:
                raise ValueError(f"Native singleton name mismatch at {raw_offset:#x}")
            image[raw_offset:raw_offset + len(old)] = new
            changes.append({"kind": "singleton_name", "offset": raw_offset,
                            "length": len(old), "before": old_name, "after": new_name})

    checksum_offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", image, checksum_offset, 0)
    checksum = pefile.PE(data=image).generate_checksum()
    struct.pack_into("<I", image, checksum_offset, checksum)
    changes.append({"kind": "checksum", "offset": checksum_offset, "length": 4})
    permitted = {i for change in changes for i in range(change["offset"], change["offset"] + change["length"])}
    actual = [i for i, (a, b) in enumerate(zip(original, image)) if a != b]
    if len(original) != len(image) or any(i not in permitted for i in actual):
        raise ValueError("Unexpected bytes changed outside the explicit test ranges")
    checked = pefile.PE(data=image)
    if checked.OPTIONAL_HEADER.CheckSum != checked.generate_checksum():
        raise ValueError("PE checksum verification failed")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    report = {"source": str(source), "output": str(output), "source_sha256": digest(original),
              "test_sha256": digest(image), "same_file_size": True,
              "execution_level": "asInvoker", "ui_access": False,
              "manifest": {"offset": offset, "size": size, "language": language},
              "isolated_instance": isolate_instance, "instance_tag": tag,
              "changed_byte_count": len(actual), "permitted_changes": changes,
              "checksum": hex(checksum), "process_launched": False}
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Bare patched AMD64 payload")
    parser.add_argument("output", type=Path, help="New executable path inside the repository")
    parser.add_argument("--isolate-instance", action="store_true")
    parser.add_argument("--instance-tag", help="Exactly 15 ASCII letters/digits/underscores")
    args = parser.parse_args()
    if args.instance_tag and not args.isolate_instance:
        parser.error("--instance-tag requires --isolate-instance")
    print(json.dumps(make(args.source, args.output, args.isolate_instance, args.instance_tag), indent=2))


if __name__ == "__main__":
    main()
