# Contributing a mod

1. Fork the repo and make your mod in `mods/<your-mod>/` only. Run
   `python3 tools/verify_repo.py` first to make sure your copy isn't edited.
2. Follow [AGENTS.md](AGENTS.md) (allowed / not allowed) and [docs/GUIDE.md](docs/GUIDE.md).
3. Run `python3 tools/check_mod.py` and build with `./build.sh`.
4. Test it on your headset with the newest blaze APK from the Discord.
5. Open a pull request and fill in the checklist.

Every pull request is checked automatically. The check fails if:
- the code matches anything on the "never add" list, or
- the pull request changes anything outside `mods/` (the SDK, the rules, the check
  and the workflows are changed only by the owner), or
- it contains binaries (only source code is accepted; mods are built here), or
- it adds an AI instruction file (`AGENTS.md`, `CLAUDE.md`, `.cursorrules`, `.claude/`...).

The owner reviews every pull request before it is merged. Builds of merged mods are
attached to each run under the repo's **Actions** tab.

## Reporting a bad mod

If a mod lags, crashes, griefs or does anything sketchy, report it in the
Discord: https://discord.gg/UfYxE9W9dX
