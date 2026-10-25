# ADR-0011: The macOS client bundles its runtime dependencies

Status: accepted
Date: 2026-10-25

## Context

The macOS client renders through dxvk-native on KosmicKrisp, and took Mesa,
the Vulkan loader, SDL3, OpenAL Soft and FFmpeg from Homebrew; Mesa was
never bundled. 0.1.0 ships that client (ADR-0010), and on 2026-10-25 the
owner ordered the client packaged so that it runs without Homebrew.

## Decision

- The `.app` carries its runtime: Mesa built with only KosmicKrisp (the
  driver and its ICD JSON), the Khronos Vulkan loader, the jm2/dxvk
  kisakcod-macos dylibs, SDL3, OpenAL Soft and a minimal FFmpeg (only the
  Bink and audio decoders and demuxers the engine uses, in an LGPL
  configuration).
- Each is built from source, pinned by version and hash in `scripts/extern/`
  as dxvk is, never copied from Homebrew.
- The bundle is relocatable: install names use `@rpath`/`@loader_path`, and
  the bundled loader finds the bundled ICD by default (`VK_DRIVER_FILES`
  still overrides it). The client minimum stays macOS 26.
- Every dylib and the app are ad-hoc signed ([ADR-0007](0007-macos-notarization-deferred.md)).
  The package README gives the first-launch Open / quarantine step.
- The bundle ships each component's license text and a sources note
  (FFmpeg and OpenAL Soft are LGPL).

## Consequences

- Done-test: on a clean macOS 26 account with no Homebrew in PATH, the
  `.app` reaches the menu, joins a LAN server and plays a round, and
  `otool -L` shows no `/opt/homebrew` path.
- A Mesa, loader or FFmpeg update is a pin bump with a re-run of that test.
