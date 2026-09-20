import argparse
import csv
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PATTERNS = {
    "image-or-offset": re.compile(r"\b(?:GetModuleHandle\w*|RtlLookupFunctionEntry|\w*(?:Rva|RVA))\b|0x[0-9a-fA-F]{5,}"),
    "pointer-access": re.compile(r"reinterpret_cast\s*<|static_cast\s*<[^>]*\*|\b\w+\s*\*\s*(?:const\s+)?\w+|\buintptr_t\b"),
    "detour-or-write": re.compile(r"\b(?:VirtualProtect|VirtualAlloc|MH_\w+|Interlocked\w+|WriteProcessMemory|safe_write_bytes)\s*\("),
    "probe-or-signature": re.compile(r"\b(?:VirtualQuery|ReadProcessMemory|memcmp|strcmp|__try|safe_read_bytes|readable\w*|writable\w*|compareImage|hashImage)\b"),
    "publication": re.compile(r"\b(?:volatile|thread_local)\b|std::atomic|\.store\(|\.load\("),
}


def inventory(root):
    for directory in sorted((root / "src").iterdir()):
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if path.suffix not in (".cpp", ".h", ".inc"):
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8-sig", errors="backslashreplace").splitlines(), 1):
                text = line.strip()
                if text.startswith(("//", "/*")):
                    continue
                kinds = [kind for kind, pattern in PATTERNS.items() if pattern.search(line)]
                if kinds:
                    yield (directory.name, path.relative_to(root).as_posix(), number, ";".join(kinds), text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = list(inventory(ROOT))
    files = []
    for scope in sorted({row[0] for row in rows}):
        selected = [row[1:] for row in rows if row[0] == scope]
        for offset in range(0, len(selected), 700):
            path = args.output / f"{scope}-{offset // 700 + 1}.csv"
            with path.open("w", newline="", encoding="utf-8") as output:
                writer = csv.writer(output)
                writer.writerow(("path", "line", "categories", "source"))
                writer.writerows(selected[offset:offset + 700])
            files.append(path.name)
    print(f"Indexed {len(rows)} source occurrences in {len(files)} CSV files")
    for name in files:
        print(name)


if __name__ == "__main__":
    main()
