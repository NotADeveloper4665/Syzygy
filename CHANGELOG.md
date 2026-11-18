# Changelog

Notable repository changes are recorded here; entries do not imply unverified runtime behavior.

## Unreleased

### Development process
- Introduced specification-led development artifacts: requirements, design, task tracking, and verification.

### Linux headless session
- Added explicit requirements and acceptance criteria for private Wayland/KWin startup, isolated touch routing, private audio, and owned-resource cleanup.
- Expanded FUSE cleanup tests for missing utilities, mounts that survive both unmount attempts, and escaped mountinfo paths.
- Existing implementation remains subject to target-host acceptance; see TESTING.md.
