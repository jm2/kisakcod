# ADR-0007: macOS releases are ad-hoc signed; notarization is deferred

Status: accepted
Date: 2026-09-30

## Context

The charter and G6 required macOS arm64 packages to be signed and notarized.
Notarization needs an Apple Developer ID, a paid account the owner would hold,
and secrets in the release workflow. The owner's release reference is openQ4 as
it is today, and on 2026-09-30 they called signing and notarization aspirational,
not a target.

## Decision

- macOS release packages are ad-hoc signed (`codesign --sign -`) after `strip`,
  so Apple silicon runs them, and are not notarized.
- The package README says so and gives the `xattr -d com.apple.quarantine`
  step for a downloaded copy.
- G6 counts macOS arm64 as shipped when the ad-hoc-signed packages pass the
  same checks as the other targets: smoke run from the unpacked package,
  symbols (a dSYM whose UUID matches), and provenance.
- Developer ID signing and notarization stay a later, optional step that the
  owner starts. The release workflow keeps no Apple credentials until then.

## Consequences

- No Apple account, certificates or notary secrets are needed in CI.
- Users see Gatekeeper's unidentified-developer prompt, or remove the
  quarantine attribute by hand. This is documented and accepted.
- If notarization is adopted later, a new ADR supersedes this one and adds
  the signing step and its secrets to `release.yml`.
