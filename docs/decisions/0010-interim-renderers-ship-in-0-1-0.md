# ADR-0010: 0.1.0 ships the interim renderers; Vulkan comes after

Status: accepted
Date: 2026-10-25

## Context

The charter made native Vulkan the release renderer, and ADR-0003 counted
the Win64 D3D9 client and dxvk-native as test vehicles, never deliverables.
By 2026-10-25 those clients play on fork servers and on Steam 1.8 servers on
every 64-bit target with no ASan reports (G5, #184). On 2026-10-25 the owner
said: "Take native vulkan renderer out of the current release gate; it'll
come after 0.1.0."

## Decision

- 0.1.0 ships the existing renderer: D3D9 on Windows (amd64 and arm64) and
  D3D9 through dxvk-native on Linux and macOS (KosmicKrisp on macOS).
- These clients are deliverables for 0.1.0. This supersedes ADR-0003's
  "never as deliverables" for the Win64 D3D9 client and dxvk-native, and
  only for them.
- A native Vulkan renderer (#713) is post-0.1.0 work, outside G5 and G6.

## Consequences

- G5's exit is the interim clients on fork and Steam 1.8 servers; G6
  packages them.
- The client release recipe has to bundle dxvk-native, and KosmicKrisp is
  a macOS runtime dependency.
- A later ADR sets Vulkan's route and gate when that work starts.
