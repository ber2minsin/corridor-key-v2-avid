## Summary

<!-- What does this PR change, and why? -->

## Type of change

- [ ] Bug fix
- [ ] New feature / Effect Editor control
- [ ] Packaging / installer
- [ ] Runtime / IPC / inference worker
- [ ] Docs / templates
- [ ] Tests
- [ ] Other (describe):

## Upstream Corridor Key 2.0

Does this depend on or mirror behavior from [nikopueringer/CorridorKey2.0](https://github.com/nikopueringer/CorridorKey2.0)?

- [ ] No — Avid / packaging only
- [ ] Yes — mirrors upstream behavior
- [ ] Yes — requires a newer submodule pin

Upstream reference (commit / PR / issue), if any:

```
<!-- e.g. nikopueringer/CorridorKey2.0@abcdef0 or submodule bump notes -->
```

## Test plan

- [ ] Built plugin (`scripts\build_plugin.ps1`) and/or installer as needed
- [ ] Quit Media Composer, deployed/installed, restarted MC
- [ ] Effect Palette → Image → **Corridor Key 2.0** still appears
- [ ] Verified on a greenscreen clip (note Mode / Output / Time Awareness settings)
- [ ] Checked `%LOCALAPPDATA%\CorridorKeyAVX\logs\` for errors (if relevant)
- [ ] Automated tests (`scripts\run_all_tests.ps1`) if this touches runtime/IPC

## Notes for reviewers

<!-- Risks, follow-ups, screenshots -->
