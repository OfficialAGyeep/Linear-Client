#!/usr/bin/env python3
"""Check that this copy of the repo is not edited.

Downloads the official repo from GitHub and compares every file outside mods/
(the rules, the SDK, the tools, the docs, the build files) byte for byte with
this copy. Mods in mods/ are yours to change, so they are not compared.

  python3 tools/verify_repo.py

Exit code 0 = matches the official repo, 1 = edited, 2 = could not reach GitHub.
"""
import io
import pathlib
import sys
import tarfile
import urllib.request

OFFICIAL = "https://github.com/Blazeisodd/Blazes-Apk-mod-menu-addon"
TARBALL = "https://codeload.github.com/Blazeisodd/Blazes-Apk-mod-menu-addon/tar.gz/refs/heads/main"
ROOT = pathlib.Path(__file__).resolve().parent.parent
SKIP_DIRS = {"mods", "build", "out", ".git"}


def official_files():
    with urllib.request.urlopen(TARBALL, timeout=60) as r:
        data = r.read()
    files = {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        for m in tar.getmembers():
            if not m.isfile():
                continue
            rel = m.name.split("/", 1)[1] if "/" in m.name else m.name
            if rel.split("/", 1)[0] in SKIP_DIRS:
                continue
            files[rel] = tar.extractfile(m).read()
    return files


def local_files():
    files = {}
    for p in ROOT.rglob("*"):
        rel = p.relative_to(ROOT).as_posix()
        if not p.is_file() or rel.split("/", 1)[0] in SKIP_DIRS or "__pycache__" in rel:
            continue
        files[rel] = p.read_bytes()
    return files


AI_NAMES = {"agents.md", "agents.override.md", "claude.md", "claude.local.md", "gemini.md",
            ".cursorrules", ".windsurfrules", "copilot-instructions.md", ".clinerules"}
AI_DIRS = {".claude", ".codex", ".cursor", ".gemini", ".windsurf", ".clinerules"}


def is_ai_file(rel):
    parts = rel.lower().split("/")
    return parts[-1] in AI_NAMES or parts[-1].endswith(".mdc") or any(d in AI_DIRS for d in parts[:-1])


def all_files():
    return [p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
            if p.is_file() and not p.relative_to(ROOT).as_posix().startswith((".git/", "build/", "out/"))]


def main():
    try:
        official = official_files()
    except Exception as e:
        print(f"Could not download {OFFICIAL} ({e}).")
        print("Can't confirm this copy is the official one. Get a fresh copy from that link.")
        sys.exit(2)
    local = local_files()
    changed = sorted(f for f in official if f in local and local[f] != official[f])
    missing = sorted(f for f in official if f not in local)
    protected = ("sdk/", "tools/", ".github/", "docs/")
    extra = sorted(f for f in local if f not in official and f.startswith(protected))
    # Files that give AI assistants instructions. Only the official AGENTS.md and
    # CLAUDE.md may exist; an extra one (also inside mods/) could change the rules.
    ai_files = sorted(f for f in all_files() if is_ai_file(f) and f not in official)
    if not (changed or missing or extra or ai_files):
        print(f"OK: this copy matches the official repo ({OFFICIAL}).")
        return
    print(f"EDITED: this copy does NOT match the official repo ({OFFICIAL}).")
    for f in changed:
        print(f"  changed: {f}")
    for f in missing:
        print(f"  missing: {f}")
    for f in extra:
        print(f"  added:   {f}")
    for f in ai_files:
        print(f"  AI instruction file that isn't official: {f}")
    print("Do not use these files. Download a fresh copy from the official link,")
    print("then move your own mods/<your-mod>/ folder into it.")
    sys.exit(1)


if __name__ == "__main__":
    main()
