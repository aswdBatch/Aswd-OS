# Release Workflow

- After every completed feature, fix, or UI update, write a short plain-language summary of what changed.
- Append that summary to the built-in changelog in `src/common/changelog.c`, because the `OS Info` app reads its release notes from that file.
- Keep the newest changelog entry aligned with `src/common/config.h` when a version bump happens.
- Mention the changelog update in the final handoff so the release trail stays visible.
