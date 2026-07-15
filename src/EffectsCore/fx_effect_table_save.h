#pragma once

#include <cstddef>
#include <cstdint>

namespace fx::archive
{
struct EffectTableSaveSnapshot;

enum class EffectTableSaveStatus : std::uint8_t
{
    Success,
    InvalidArgument,
    InvalidState,
    NameTooLong,
    InvalidName,
    CapacityExceeded,
    InvalidKey,
    ConflictingDuplicate,
    WriterFailed,
};

using EffectTableSaveWriterCallback =
    bool (*)(void *context, const void *data, std::size_t byteCount) noexcept;

struct EffectTableSaveCallbacks
{
    void *context = nullptr;
    EffectTableSaveWriterCallback write = nullptr;
};

[[nodiscard]] std::size_t EffectTableSaveSnapshotSize() noexcept;
[[nodiscard]] std::size_t EffectTableSaveSnapshotAlignment() noexcept;

// Constructs the opaque snapshot in aligned caller-owned storage. The
// storage must remain live and exclusively owned until the matching destroy.
// Reuse requires destroy followed by a fresh construct.
[[nodiscard]] EffectTableSaveSnapshot *ConstructEffectTableSaveSnapshot(
    void *storage,
    std::size_t storageSize) noexcept;
// Null is an already-destroyed success. Destruction is refused only while a
// writer callback is active, preventing callback-driven lifetime reentry.
[[nodiscard]] bool DestroyEffectTableSaveSnapshot(
    EffectTableSaveSnapshot *snapshot) noexcept;

// The snapshot is a one-shot Capturing -> Validated -> Written state machine.
// An operation used in the wrong phase records InvalidState. The first
// failure is sticky, and later operations return it without doing more work.
[[nodiscard]] EffectTableSaveStatus AppendEffectTableSaveEntryNoReport(
    EffectTableSaveSnapshot *snapshot,
    const char *name,
    std::uintptr_t key) noexcept;
[[nodiscard]] EffectTableSaveStatus ValidateEffectTableSaveSnapshotNoReport(
    EffectTableSaveSnapshot *snapshot) noexcept;

// A validated snapshot is also the admission set for definition pointers in
// the separately captured FX graph.  This lookup compares only the numeric
// key and never dereferences the candidate pointer.  It fails closed outside
// the Validated phase so callers cannot prove graph ownership against a table
// that is incomplete, failed, or already being written.
[[nodiscard]] bool EffectTableSaveSnapshotContainsKey(
    const EffectTableSaveSnapshot *snapshot,
    std::uintptr_t key) noexcept;

[[nodiscard]] EffectTableSaveStatus WriteEffectTableSaveSnapshotNoReport(
    EffectTableSaveSnapshot *snapshot,
    const EffectTableSaveCallbacks &callbacks) noexcept;

// Returns the number of entries captured before any failure. A null snapshot
// has no entries.
[[nodiscard]] std::size_t EffectTableSaveEntryCount(
    const EffectTableSaveSnapshot *snapshot) noexcept;
} // namespace fx::archive
