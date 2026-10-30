#!/usr/bin/env python3
"""Safety check for mods. Runs in build.sh/build.bat and on every pull request.

It FAILS on code that can hurt other players, the server or someone's account:
network/RPC sending, kicks/bans/votes, economy, identity/credentials, outside
network access, running other code, memory tricks, and obfuscation. It WARNS on
things that often cause lag. The rules live in AGENTS.md ("Never add").

This is a tripwire, not a full review: a human maintainer still reviews every PR.
Usage: python3 tools/check_mod.py [paths...]   (default: mods/)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE_EXT = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".txt", ".cmake"}
AI_NAMES = {"agents.md", "agents.override.md", "claude.md", "claude.local.md", "gemini.md",
            ".cursorrules", ".windsurfrules", "copilot-instructions.md", ".clinerules"}
AI_DIRS = {".claude", ".codex", ".cursor", ".gemini", ".windsurf", ".clinerules"}
BINARY_EXT = {".so", ".a", ".o", ".apk", ".dex", ".jar", ".bin", ".exe", ".dll", ".zip", ".rar", ".7z"}

FAIL = [
    # Sending things to other players / the room (laggers, crashers, grief)
    (r"PhotonView|PhotonNetwork|LoadBalancing|RaiseEvent|OpRaiseEvent|\bRPC\b|RpcTarget"
     r"|SendAllOutgoing|RunViewUpdate|NetworkMessenger|SendExecute|\bRpc[A-Z]\w*", "network / RPC sending"),
    (r"MasterClient|SetMasterClient", "taking master client"),
    (r"(?i)\bkick|\bban(?:ned|s)?\b|setban|isbanned|gamebanned|permaban|unban|\bvote|moderat|reportplayer"
     r"|mute(?:player|ban)", "kicks, bans, votes or moderation"),
    (r"GameManager", "GameManager (creative mode / room control)"),
    (r"Purchase|Redeem|Currency|Butt ?[Cc]oin|CashOut|Stash|UnlockAll|IsPatternOwned|ownedPatterns",
     "economy / unlocking items"),
    # Identity and accounts
    (r"(?i)spoof|device_?id|deviceUniqueIdentifier|access_?token|access_?key|secret_?key|password"
     r"|Oculus\.Platform|oculus_?id|meta_?id|UserProof|Entitlement|AccountManager|account_?data|device_?key"
     r"|AuthValues|\buser_?id\b|PlayerPrefs", "account, identity or credentials"),
    # Device spoofers and ban evasion
    (r"(?i)hwid|SystemInfo|device(?:Name|Model)|android_?id|Settings\$Secure|Secure\.getString"
     r"|__system_property|getprop|ro\.serialno|ro\.boot\.|Build\.SERIAL|\bserial_?(?:no|number)?\b"
     r"|mac_?addr|wlan0|evad(?:e|ing|ion)|ban_?bypass|alt_?account|new_?account|fresh_?account",
     "device spoofing or ban evasion"),
    # Outside network and running other code
    (r"\bsocket\s*\(|\bconnect\s*\(|getaddrinfo|gethostbyname|\bcurl|libcurl|UnityWebRequest|WebClient"
     r"|HttpClient|https?://", "network access / URLs"),
    (r"\bsystem\s*\(|\bpopen\s*\(|\bexec[lv]p?e?\s*\(|\bfork\s*\(|dlopen|dlsym|android_dlopen",
     "running other programs or loading other code"),
    (r"mprotect|mmap|/proc/|ptrace|process_vm_|DobbyHook|__builtin___clear_cache", "raw memory / process tricks"),
    (r"\bfopen\s*\([^)]*\"[wa]|ofstream|\bopen\s*\([^)]*O_(?:WRONLY|RDWR|CREAT)", "writing files"),
    (r"base64|\\x[0-9a-fA-F]{2}(?:\\x[0-9a-fA-F]{2}){7,}|\bxor\b|obfuscat", "obfuscated code"),
]
WARN = [
    (r"FindObjectsOfType|FindObjectOfType|FindAnyObjectByType|FindObjectsByType|Resources\.FindObjectsOfTypeAll"
     r"|GameObject\.Find\b", "scene search: never call it every frame, cache the result"),
    (r"while\s*\(\s*(?:true|1)\s*\)|for\s*\(\s*;\s*;\s*\)", "endless loop"),
    (r"std::thread|pthread_create|std::async", "threads: Unity may only be used on the main thread"),
    (r"\bsleep\s*\(|usleep|nanosleep|sleep_for", "sleeping blocks the game's frame"),
    (r"CreatePrimitive|Instantiate", "creating objects: never every frame, keep a hard limit"),
]


def scan(paths):
    failed = warned = 0
    for base in paths:
        files = [base] if base.is_file() else [p for p in base.rglob("*") if p.is_file()]
        for f in files:
            rel = f.relative_to(ROOT) if f.is_relative_to(ROOT) else f
            low = [x.lower() for x in rel.parts]
            if (low[-1] in AI_NAMES or low[-1].endswith(".mdc") or any(d in AI_DIRS for d in low[:-1])):
                print(f"FAIL {rel}: AI instruction file in a mod folder (only the owner's AGENTS.md/CLAUDE.md count)")
                failed += 1
                continue
            if f.suffix.lower() in BINARY_EXT:
                print(f"FAIL {rel}: binary file in a mod folder (only source code is accepted)")
                failed += 1
                continue
            if f.suffix.lower() not in SOURCE_EXT and f.name != "CMakeLists.txt":
                continue
            for n, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
                for pat, why in FAIL:
                    if re.search(pat, line):
                        print(f"FAIL {rel}:{n}: {why}\n     {line.strip()[:160]}")
                        failed += 1
                for pat, why in WARN:
                    if re.search(pat, line):
                        print(f"warn {rel}:{n}: {why}")
                        warned += 1
    return failed, warned


def main():
    args = [pathlib.Path(a).resolve() for a in sys.argv[1:]] or [ROOT / "mods"]
    failed, warned = scan(args)
    print(f"\ncheck_mod: {failed} problem(s), {warned} warning(s)")
    if failed:
        print("Not allowed in this repo. See AGENTS.md -> 'Never add'. Questions: https://discord.gg/UfYxE9W9dX")
        sys.exit(1)


if __name__ == "__main__":
    main()
