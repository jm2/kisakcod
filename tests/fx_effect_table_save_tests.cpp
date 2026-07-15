#include "fx_effect_table_save.h"

#include "fx_effect_table_restore.h"

#include <universal/memfile.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__) || defined(__unix__)
#include <pthread.h>
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer) \
    || __has_feature(memory_sanitizer) \
    || __has_feature(thread_sanitizer)
#define KISAK_SAVE_TEST_STACK_SANITIZER 1
#endif
#endif

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define KISAK_SAVE_TEST_STACK_SANITIZER 1
#endif

namespace
{
namespace archive = fx::archive;

std::atomic<int> failures;
std::atomic<int> unexpectedReports;
std::thread::id mainThreadId;

void Check(const bool condition, const char *const expression, const int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    failures.fetch_add(1, std::memory_order_relaxed);
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

struct EntrySpec
{
    std::string name;
    std::uintptr_t key;
};

void AppendLittleEndianU32(
    std::vector<std::uint8_t> &output,
    const std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
    output.push_back(static_cast<std::uint8_t>(value >> 16u));
    output.push_back(static_cast<std::uint8_t>(value >> 24u));
}

std::vector<std::uint8_t> MakeLogicalBytes(
    const std::vector<EntrySpec> &entries)
{
    std::vector<std::uint8_t> output;
    for (const EntrySpec &entry : entries)
    {
        output.insert(output.end(), entry.name.begin(), entry.name.end());
        output.push_back(0);
        AppendLittleEndianU32(
            output, static_cast<std::uint32_t>(entry.key));
    }
    output.push_back(0);
    return output;
}

std::vector<std::vector<std::uint8_t>> MakeLogicalChunks(
    const std::vector<EntrySpec> &entries)
{
    std::vector<std::vector<std::uint8_t>> chunks;
    chunks.reserve(entries.size() * 2u + 1u);
    for (const EntrySpec &entry : entries)
    {
        std::vector<std::uint8_t> name(
            entry.name.begin(), entry.name.end());
        name.push_back(0);
        chunks.push_back(std::move(name));

        std::vector<std::uint8_t> key;
        key.reserve(sizeof(std::uint32_t));
        AppendLittleEndianU32(
            key, static_cast<std::uint32_t>(entry.key));
        chunks.push_back(std::move(key));
    }
    chunks.push_back({0});
    return chunks;
}

class SnapshotStorage
{
public:
    SnapshotStorage()
        : snapshotSize_(archive::EffectTableSaveSnapshotSize()),
          snapshotAlignment_(archive::EffectTableSaveSnapshotAlignment()),
          storage_(snapshotSize_ + snapshotAlignment_ - 1u
              + 2u * GUARD_SIZE,
              CANARY)
    {
        CHECK(snapshotSize_ != 0);
        CHECK(snapshotAlignment_ != 0);
        CHECK((snapshotAlignment_ & (snapshotAlignment_ - 1u)) == 0);
        if (snapshotSize_ == 0 || snapshotAlignment_ == 0)
            return;

        const std::uintptr_t base =
            reinterpret_cast<std::uintptr_t>(storage_.data());
        const std::uintptr_t candidate = base + GUARD_SIZE;
        const std::uintptr_t aligned =
            (candidate + snapshotAlignment_ - 1u)
            & ~(static_cast<std::uintptr_t>(snapshotAlignment_ - 1u));
        snapshotOffset_ = static_cast<std::size_t>(aligned - base);
        CHECK(snapshotOffset_ >= GUARD_SIZE);
        CHECK(snapshotOffset_ + snapshotSize_ + GUARD_SIZE
            <= storage_.size());
        if (snapshotOffset_ + snapshotSize_ > storage_.size())
            return;

        snapshot_ = archive::ConstructEffectTableSaveSnapshot(
            storage_.data() + snapshotOffset_, snapshotSize_);
        CHECK(snapshot_ != nullptr);
        CheckCanaries();
    }

    ~SnapshotStorage()
    {
        if (snapshot_)
        {
            CHECK(archive::DestroyEffectTableSaveSnapshot(snapshot_));
            snapshot_ = nullptr;
        }
        CheckCanaries();
    }

    SnapshotStorage(const SnapshotStorage &) = delete;
    SnapshotStorage &operator=(const SnapshotStorage &) = delete;

    archive::EffectTableSaveSnapshot *get() noexcept
    {
        return snapshot_;
    }

    void CheckCanaries() const
    {
        for (std::size_t index = 0; index < snapshotOffset_; ++index)
            CHECK(storage_[index] == CANARY);
        const std::size_t suffix = snapshotOffset_ + snapshotSize_;
        for (std::size_t index = suffix; index < storage_.size(); ++index)
            CHECK(storage_[index] == CANARY);
    }

private:
    static constexpr std::size_t GUARD_SIZE = 64;
    static constexpr std::uint8_t CANARY = 0xA5u;

    std::size_t snapshotSize_ = 0;
    std::size_t snapshotAlignment_ = 0;
    std::vector<std::uint8_t> storage_;
    std::size_t snapshotOffset_ = 0;
    archive::EffectTableSaveSnapshot *snapshot_ = nullptr;
};

void AppendEntries(
    archive::EffectTableSaveSnapshot *const snapshot,
    const std::vector<EntrySpec> &entries)
{
    for (const EntrySpec &entry : entries)
    {
        CHECK(archive::AppendEffectTableSaveEntryNoReport(
            snapshot, entry.name.c_str(), entry.key)
            == archive::EffectTableSaveStatus::Success);
    }
}

struct ByteWriterState
{
    explicit ByteWriterState(
        const std::size_t byteCapacity,
        const std::size_t callCapacity)
        : bytes(byteCapacity, 0xEEu),
          callSizes(callCapacity, 0)
    {
    }

    std::vector<std::uint8_t> bytes;
    std::vector<std::size_t> callSizes;
    std::size_t bytesUsed = 0;
    std::size_t callCount = 0;
    std::size_t failAt = (std::numeric_limits<std::size_t>::max)();
    bool invalidCall = false;
    archive::EffectTableSaveSnapshot *snapshotToDestroy = nullptr;
    bool attemptedDestroy = false;
    bool destroyResult = true;
};

bool CaptureBytes(
    void *const context,
    const void *const data,
    const std::size_t byteCount) noexcept
{
    auto *const state = static_cast<ByteWriterState *>(context);
    const std::size_t call = state->callCount;
    ++state->callCount;
    if (!data || byteCount == 0 || call >= state->callSizes.size())
    {
        state->invalidCall = true;
        return false;
    }
    state->callSizes[call] = byteCount;

    if (call == 0 && state->snapshotToDestroy)
    {
        state->attemptedDestroy = true;
        state->destroyResult =
            archive::DestroyEffectTableSaveSnapshot(
                state->snapshotToDestroy);
    }
    if (call == state->failAt)
        return false;
    if (state->bytesUsed > state->bytes.size()
        || byteCount > state->bytes.size() - state->bytesUsed)
    {
        state->invalidCall = true;
        return false;
    }

    std::memcpy(
        state->bytes.data() + state->bytesUsed,
        data,
        byteCount);
    state->bytesUsed += byteCount;
    return true;
}

archive::EffectTableSaveCallbacks Callbacks(
    ByteWriterState *const state) noexcept
{
    return {state, CaptureBytes};
}

std::vector<std::uint8_t> ObservedBytes(const ByteWriterState &state)
{
    return {state.bytes.begin(),
            state.bytes.begin()
                + static_cast<std::ptrdiff_t>(state.bytesUsed)};
}

void CheckSuccessfulWrite(
    const std::vector<EntrySpec> &entries,
    const bool attemptDestroyDuringWrite = false)
{
    SnapshotStorage storage;
    archive::EffectTableSaveSnapshot *const snapshot = storage.get();
    CHECK(snapshot != nullptr);
    AppendEntries(snapshot, entries);
    CHECK(archive::EffectTableSaveEntryCount(snapshot) == entries.size());
    CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(snapshot)
        == archive::EffectTableSaveStatus::Success);

    const std::vector<std::uint8_t> expected =
        MakeLogicalBytes(entries);
    const std::vector<std::vector<std::uint8_t>> chunks =
        MakeLogicalChunks(entries);
    ByteWriterState writer(expected.size(), chunks.size());
    if (attemptDestroyDuringWrite)
        writer.snapshotToDestroy = snapshot;
    CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
        snapshot, Callbacks(&writer))
        == archive::EffectTableSaveStatus::Success);
    CHECK(!writer.invalidCall);
    CHECK(writer.callCount == chunks.size());
    CHECK(writer.bytesUsed == expected.size());
    CHECK(ObservedBytes(writer) == expected);
    for (std::size_t index = 0; index < chunks.size(); ++index)
        CHECK(writer.callSizes[index] == chunks[index].size());
    if (attemptDestroyDuringWrite)
    {
        CHECK(writer.attemptedDestroy);
        CHECK(!writer.destroyResult);
    }

    ByteWriterState secondWriter(1u, 1u);
    CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
        snapshot, Callbacks(&secondWriter))
        == archive::EffectTableSaveStatus::InvalidState);
    CHECK(secondWriter.callCount == 0);
    CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
        snapshot, Callbacks(&secondWriter))
        == archive::EffectTableSaveStatus::InvalidState);
    CHECK(secondWriter.callCount == 0);
    storage.CheckCanaries();
}

std::vector<EntrySpec> MakeCapacityEntries(const std::size_t count)
{
    std::vector<EntrySpec> entries;
    entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        entries.push_back({
            "fx/e" + std::to_string(index),
            static_cast<std::uintptr_t>(index + 1u),
        });
    }
    return entries;
}

void TestConstructionAndCanaries()
{
    const std::size_t size = archive::EffectTableSaveSnapshotSize();
    const std::size_t alignment =
        archive::EffectTableSaveSnapshotAlignment();
    CHECK(size != 0);
    CHECK(alignment != 0);
    CHECK((alignment & (alignment - 1u)) == 0);
    CHECK(archive::EffectTableSaveEntryCount(nullptr) == 0);
    CHECK(archive::DestroyEffectTableSaveSnapshot(nullptr));
    CHECK(archive::ConstructEffectTableSaveSnapshot(nullptr, size)
        == nullptr);

    std::vector<std::uint8_t> raw(size + alignment + 1u, 0xC7u);
    const std::uintptr_t base =
        reinterpret_cast<std::uintptr_t>(raw.data());
    const std::uintptr_t aligned =
        (base + alignment - 1u)
        & ~(static_cast<std::uintptr_t>(alignment - 1u));
    auto *const alignedStorage = reinterpret_cast<void *>(aligned);
    CHECK(archive::ConstructEffectTableSaveSnapshot(
        alignedStorage, size - 1u) == nullptr);
    if (alignment > 1u)
    {
        auto *const misaligned = reinterpret_cast<void *>(aligned + 1u);
        CHECK(archive::ConstructEffectTableSaveSnapshot(
            misaligned, size) == nullptr);
    }

    SnapshotStorage storage;
    CHECK(archive::EffectTableSaveEntryCount(storage.get()) == 0);
    storage.CheckCanaries();
}

void TestExactLogicalBytes()
{
    const std::vector<EntrySpec> empty;
    const std::vector<EntrySpec> one{{"fx/smoke", 0x78563412u}};
    const std::vector<EntrySpec> many{
        {"fx/first", 1u},
        {"world\\dust", 0x01020304u},
        {"weapons/rifle/muzzle", 0xFFFFFFFFu},
    };
    const std::vector<EntrySpec> maximumName{{
        std::string(
            archive::EFFECT_TABLE_RESTORE_NAME_CAPACITY - 1u,
            'n'),
        0xA1B2C3D4u,
    }};
    CheckSuccessfulWrite(empty);
    CheckSuccessfulWrite(one);
    CheckSuccessfulWrite(many);
    CheckSuccessfulWrite(maximumName);
    CheckSuccessfulWrite(
        MakeCapacityEntries(archive::EFFECT_TABLE_RESTORE_CAPACITY));
    CheckSuccessfulWrite(one, true);
}

void CheckStickyCaptureFailure(
    const char *const name,
    const std::uintptr_t key,
    const archive::EffectTableSaveStatus expectedStatus)
{
    SnapshotStorage storage;
    archive::EffectTableSaveSnapshot *const snapshot = storage.get();
    CHECK(archive::AppendEffectTableSaveEntryNoReport(
        snapshot, name, key) == expectedStatus);
    CHECK(archive::EffectTableSaveEntryCount(snapshot) == 0);
    CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(snapshot)
        == expectedStatus);
    ByteWriterState writer(16, 4);
    CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
        snapshot, Callbacks(&writer)) == expectedStatus);
    CHECK(writer.callCount == 0);
    CHECK(writer.bytesUsed == 0);
    storage.CheckCanaries();
}

void CheckValidationFailure(
    const std::vector<EntrySpec> &entries,
    const archive::EffectTableSaveStatus expectedStatus)
{
    SnapshotStorage storage;
    archive::EffectTableSaveSnapshot *const snapshot = storage.get();
    AppendEntries(snapshot, entries);
    CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(snapshot)
        == expectedStatus);
    ByteWriterState writer(64, entries.size() * 2u + 1u);
    CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
        snapshot, Callbacks(&writer)) == expectedStatus);
    CHECK(writer.callCount == 0);
    CHECK(writer.bytesUsed == 0);
    storage.CheckCanaries();
}

void TestCaptureFailures()
{
    CheckStickyCaptureFailure(
        nullptr,
        1u,
        archive::EffectTableSaveStatus::InvalidArgument);

    std::array<char, archive::EFFECT_TABLE_RESTORE_NAME_CAPACITY>
        unterminated{};
    unterminated.fill('x');
    CheckStickyCaptureFailure(
        unterminated.data(),
        1u,
        archive::EffectTableSaveStatus::NameTooLong);

    const std::string sixtyFour(
        archive::EFFECT_TABLE_RESTORE_NAME_CAPACITY, 'x');
    CheckStickyCaptureFailure(
        sixtyFour.c_str(),
        1u,
        archive::EffectTableSaveStatus::NameTooLong);
    CheckStickyCaptureFailure(
        "fx/zero",
        0,
        archive::EffectTableSaveStatus::InvalidKey);

#if UINTPTR_MAX > UINT32_MAX
    {
        const std::uintptr_t tooLarge =
            static_cast<std::uintptr_t>(
                (std::numeric_limits<std::uint32_t>::max)())
            + std::uintptr_t{1};
        CheckStickyCaptureFailure(
            "fx/wide",
            tooLarge,
            archive::EffectTableSaveStatus::InvalidKey);
    }
#endif

    SnapshotStorage storage;
    archive::EffectTableSaveSnapshot *const snapshot = storage.get();
    const std::vector<EntrySpec> entries = MakeCapacityEntries(
        archive::EFFECT_TABLE_RESTORE_CAPACITY);
    AppendEntries(snapshot, entries);
    CHECK(archive::EffectTableSaveEntryCount(snapshot)
        == archive::EFFECT_TABLE_RESTORE_CAPACITY);
    CHECK(archive::AppendEffectTableSaveEntryNoReport(
        snapshot, "fx/overflow", 2001u)
        == archive::EffectTableSaveStatus::CapacityExceeded);
    CHECK(archive::EffectTableSaveEntryCount(snapshot)
        == archive::EFFECT_TABLE_RESTORE_CAPACITY);
    ByteWriterState writer(16, 1);
    CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
        snapshot, Callbacks(&writer))
        == archive::EffectTableSaveStatus::CapacityExceeded);
    CHECK(writer.callCount == 0);
    storage.CheckCanaries();
}

void TestInvalidNames()
{
    std::string controlName{"fx/"};
    controlName.push_back(static_cast<char>(0x1Fu));
    controlName.push_back('x');
    const std::vector<std::string> invalidNames{
        "",
        "/absolute",
        "\\absolute",
        "fx//smoke",
        "fx\\\\smoke",
        "./smoke",
        "../smoke",
        "fx/./smoke",
        "fx/../smoke",
        "fx/smoke.",
        "fx/smoke ",
        "C:smoke",
        "CON",
        "fx/NUL.txt",
        "COM1.bin",
        "LPT9",
        "fx/bad\"name",
        "fx/bad<name",
        "fx/bad>name",
        "fx/bad|name",
        "fx/bad?name",
        "fx/bad*name",
        controlName,
    };
    for (const std::string &name : invalidNames)
    {
        CheckValidationFailure(
            {{name, 1u}}, archive::EffectTableSaveStatus::InvalidName);
    }
}

void TestDuplicatePolicies()
{
    CheckValidationFailure(
        {{"fx/first", 19u}, {"fx/second", 19u}},
        archive::EffectTableSaveStatus::ConflictingDuplicate);

    const std::vector<EntrySpec> exactDuplicates{
        {"fx/repeated", 21u},
        {"fx/repeated", 21u},
        {"fx/repeated", 21u},
    };
    CheckSuccessfulWrite(exactDuplicates);

    const std::vector<EntrySpec> sameNameDifferentKeys{
        {"fx/shared", 31u},
        {"fx/shared", 32u},
    };
    CheckSuccessfulWrite(sameNameDifferentKeys);
}

void TestInvalidStateAndMissingWriter()
{
    {
        SnapshotStorage storage;
        ByteWriterState writer(16, 2);
        CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
            storage.get(), Callbacks(&writer))
            == archive::EffectTableSaveStatus::InvalidState);
        CHECK(writer.callCount == 0);
        CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(storage.get())
            == archive::EffectTableSaveStatus::InvalidState);
    }

    {
        SnapshotStorage storage;
        archive::EffectTableSaveSnapshot *const snapshot = storage.get();
        CHECK(archive::AppendEffectTableSaveEntryNoReport(
            snapshot, "fx/state", 41u)
            == archive::EffectTableSaveStatus::Success);
        CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(snapshot)
            == archive::EffectTableSaveStatus::Success);
        CHECK(archive::AppendEffectTableSaveEntryNoReport(
            snapshot, "fx/late", 42u)
            == archive::EffectTableSaveStatus::InvalidState);
        ByteWriterState writer(32, 3);
        CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
            snapshot, Callbacks(&writer))
            == archive::EffectTableSaveStatus::InvalidState);
        CHECK(writer.callCount == 0);
    }

    {
        SnapshotStorage storage;
        archive::EffectTableSaveSnapshot *const snapshot = storage.get();
        CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(snapshot)
            == archive::EffectTableSaveStatus::Success);
        const archive::EffectTableSaveCallbacks missingWriter{};
        CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
            snapshot, missingWriter)
            == archive::EffectTableSaveStatus::InvalidArgument);
    }

    CHECK(archive::AppendEffectTableSaveEntryNoReport(
        nullptr, "fx/null", 1u)
        == archive::EffectTableSaveStatus::InvalidArgument);
    CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(nullptr)
        == archive::EffectTableSaveStatus::InvalidArgument);
    ByteWriterState writer(1, 1);
    CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
        nullptr, Callbacks(&writer))
        == archive::EffectTableSaveStatus::InvalidArgument);
    CHECK(writer.callCount == 0);
}

void TestWriterFailures()
{
    const std::vector<EntrySpec> entries{
        {"fx/first", 0x01020304u},
        {"fx/middle", 0x11223344u},
        {"fx/last", 0xA1B2C3D4u},
    };
    const std::vector<std::vector<std::uint8_t>> chunks =
        MakeLogicalChunks(entries);
    const std::vector<std::uint8_t> expected =
        MakeLogicalBytes(entries);
    const std::array<std::size_t, 5> failureCalls{
        0u, 4u, 1u, 3u, chunks.size() - 1u,
    };

    for (const std::size_t failAt : failureCalls)
    {
        SnapshotStorage storage;
        archive::EffectTableSaveSnapshot *const snapshot = storage.get();
        AppendEntries(snapshot, entries);
        CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(snapshot)
            == archive::EffectTableSaveStatus::Success);

        ByteWriterState writer(expected.size(), chunks.size());
        writer.failAt = failAt;
        CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
            snapshot, Callbacks(&writer))
            == archive::EffectTableSaveStatus::WriterFailed);
        CHECK(!writer.invalidCall);
        CHECK(writer.callCount == failAt + 1u);
        CHECK(writer.callSizes[failAt] == chunks[failAt].size());

        std::vector<std::uint8_t> expectedPrefix;
        for (std::size_t index = 0; index < failAt; ++index)
        {
            expectedPrefix.insert(
                expectedPrefix.end(),
                chunks[index].begin(),
                chunks[index].end());
        }
        CHECK(ObservedBytes(writer) == expectedPrefix);

        const std::size_t callsAfterFailure = writer.callCount;
        CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
            snapshot, Callbacks(&writer))
            == archive::EffectTableSaveStatus::WriterFailed);
        CHECK(writer.callCount == callsAfterFailure);
        storage.CheckCanaries();
    }
}

enum class ReentryOperation : std::uint8_t
{
    Append,
    Validate,
    Write,
};

struct ReentryWriterState
{
    archive::EffectTableSaveSnapshot *snapshot = nullptr;
    ReentryOperation operation = ReentryOperation::Append;
    archive::EffectTableSaveStatus nestedStatus =
        archive::EffectTableSaveStatus::Success;
    std::size_t calls = 0;
};

bool ReenterSaveSnapshot(
    void *const context,
    const void *,
    const std::size_t) noexcept
{
    auto *const state = static_cast<ReentryWriterState *>(context);
    ++state->calls;
    switch (state->operation)
    {
    case ReentryOperation::Append:
        state->nestedStatus =
            archive::AppendEffectTableSaveEntryNoReport(
                state->snapshot, "fx/reentrant", 0x42u);
        break;
    case ReentryOperation::Validate:
        state->nestedStatus =
            archive::ValidateEffectTableSaveSnapshotNoReport(
                state->snapshot);
        break;
    case ReentryOperation::Write:
    {
        const archive::EffectTableSaveCallbacks nestedCallbacks{
            state,
            ReenterSaveSnapshot,
        };
        state->nestedStatus =
            archive::WriteEffectTableSaveSnapshotNoReport(
                state->snapshot, nestedCallbacks);
        break;
    }
    }
    return true;
}

void TestWriterReentry()
{
    for (const ReentryOperation operation : {
             ReentryOperation::Append,
             ReentryOperation::Validate,
             ReentryOperation::Write,
         })
    {
        SnapshotStorage storage;
        archive::EffectTableSaveSnapshot *const snapshot = storage.get();
        CHECK(archive::AppendEffectTableSaveEntryNoReport(
            snapshot, "fx/outer", 0x41u)
            == archive::EffectTableSaveStatus::Success);
        CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(snapshot)
            == archive::EffectTableSaveStatus::Success);

        ReentryWriterState writer{snapshot, operation};
        const archive::EffectTableSaveCallbacks callbacks{
            &writer,
            ReenterSaveSnapshot,
        };
        CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
            snapshot, callbacks)
            == archive::EffectTableSaveStatus::InvalidState);
        CHECK(writer.nestedStatus
            == archive::EffectTableSaveStatus::InvalidState);
        CHECK(writer.calls == 1);
        storage.CheckCanaries();
    }
}

struct RestoreDefinition
{
    std::uint32_t id = 0;
};

struct RestoreState
{
    const void *identity = nullptr;
    std::uint32_t generation = 0;
    std::vector<std::string> names;
    std::vector<RestoreDefinition> definitions;
    std::size_t registerCalls = 0;
    bool mismatch = false;
};

bool ValidateRestoreLifecycle(
    void *const context,
    const void *const identity,
    const std::uint32_t generation) noexcept
{
    auto *const state = static_cast<RestoreState *>(context);
    if (identity != state->identity || generation != state->generation)
    {
        state->mismatch = true;
        return false;
    }
    return true;
}

const void *RegisterRestoredEffect(
    void *const context,
    const char *const name) noexcept
{
    auto *const state = static_cast<RestoreState *>(context);
    const std::size_t index = state->registerCalls;
    ++state->registerCalls;
    if (!name || index >= state->names.size()
        || state->names[index] != name
        || index >= state->definitions.size())
    {
        state->mismatch = true;
        return nullptr;
    }
    return &state->definitions[index];
}

struct MemFileWriterState
{
    MemoryFile *memFile = nullptr;
    std::size_t calls = 0;
};

bool WriteToMemFile(
    void *const context,
    const void *const data,
    const std::size_t byteCount) noexcept
{
    auto *const state = static_cast<MemFileWriterState *>(context);
    ++state->calls;
    if (!state->memFile || !data || byteCount == 0
        || byteCount > static_cast<std::size_t>(INT_MAX))
    {
        return false;
    }
    MemFile_WriteData(
        state->memFile, static_cast<int>(byteCount), data);
    return !state->memFile->memoryOverflow;
}

void CloseReader(MemoryFile &reader)
{
    if (!reader.memoryOverflow && reader.segmentIndex >= 0)
        MemFile_MoveToSegment(&reader, -1);
    MemFile_Shutdown(&reader);
}

std::vector<EntrySpec> MakeRestoredEntries(
    const std::vector<EntrySpec> &rawEntries)
{
    std::vector<EntrySpec> restored;
    restored.reserve(rawEntries.size());
    for (const EntrySpec &entry : rawEntries)
    {
        const auto duplicate = std::find_if(
            restored.begin(),
            restored.end(),
            [&entry](const EntrySpec &existing) {
                return existing.key == entry.key
                    && existing.name == entry.name;
            });
        if (duplicate == restored.end())
            restored.push_back(entry);
    }
    return restored;
}

std::vector<std::uint8_t> WriteLegacyReferenceArchive(
    const std::vector<EntrySpec> &entries,
    const bool compress)
{
    const std::vector<std::uint8_t> logical = MakeLogicalBytes(entries);
    CHECK(logical.size()
        <= (static_cast<std::size_t>(INT_MAX) - 4096u) / 3u);
    const std::size_t capacity = logical.size() * 3u + 4096u;
    std::vector<std::uint8_t> archiveBytes(capacity);

    MemoryFile writer{};
    MemFile_InitForWriting(
        &writer,
        static_cast<int>(archiveBytes.size()),
        archiveBytes.data(),
        false,
        compress);
    const std::vector<std::vector<std::uint8_t>> chunks =
        MakeLogicalChunks(entries);
    for (const std::vector<std::uint8_t> &chunk : chunks)
    {
        MemFile_WriteData(
            &writer,
            static_cast<int>(chunk.size()),
            chunk.data());
    }
    MemFile_StartSegment(&writer, -1);
    CHECK(!writer.memoryOverflow);
    CHECK(writer.bufferSize >= 0);
    if (writer.bufferSize >= 0)
        archiveBytes.resize(static_cast<std::size_t>(writer.bufferSize));
    MemFile_Shutdown(&writer);
    return archiveBytes;
}

void RunMemFileRoundTrip(
    const std::vector<EntrySpec> &entries,
    const bool compress)
{
    const std::vector<std::uint8_t> logical = MakeLogicalBytes(entries);
    CHECK(logical.size()
        <= (static_cast<std::size_t>(INT_MAX) - 4096u) / 3u);
    const std::size_t capacity = logical.size() * 3u + 4096u;
    std::vector<std::uint8_t> archiveBytes(capacity);

    SnapshotStorage storage;
    archive::EffectTableSaveSnapshot *const snapshot = storage.get();
    AppendEntries(snapshot, entries);
    CHECK(archive::ValidateEffectTableSaveSnapshotNoReport(snapshot)
        == archive::EffectTableSaveStatus::Success);

    MemoryFile writer{};
    MemFile_InitForWriting(
        &writer,
        static_cast<int>(archiveBytes.size()),
        archiveBytes.data(),
        false,
        compress);
    MemFileWriterState writerState{&writer, 0};
    const archive::EffectTableSaveCallbacks saveCallbacks{
        &writerState, WriteToMemFile};
    CHECK(archive::WriteEffectTableSaveSnapshotNoReport(
        snapshot, saveCallbacks)
        == archive::EffectTableSaveStatus::Success);
    CHECK(writerState.calls == entries.size() * 2u + 1u);
    MemFile_StartSegment(&writer, -1);
    CHECK(!writer.memoryOverflow);
    CHECK(writer.bufferSize >= 0);
    if (writer.bufferSize >= 0)
        archiveBytes.resize(static_cast<std::size_t>(writer.bufferSize));
    MemFile_Shutdown(&writer);
    CHECK(archiveBytes
        == WriteLegacyReferenceArchive(entries, compress));

    MemoryFile reader{};
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archiveBytes.size()),
        archiveBytes.data(),
        compress);
    int identityToken = 0;
    constexpr std::uint32_t generation = 0xABCDEF01u;
    RestoreState restoreState{};
    restoreState.identity = &identityToken;
    restoreState.generation = generation;
    const std::vector<EntrySpec> restoredEntries =
        MakeRestoredEntries(entries);
    restoreState.definitions.resize(restoredEntries.size());
    restoreState.names.reserve(restoredEntries.size());
    for (std::size_t index = 0; index < restoredEntries.size(); ++index)
    {
        restoreState.names.push_back(restoredEntries[index].name);
        restoreState.definitions[index].id =
            static_cast<std::uint32_t>(index + 1u);
    }
    const archive::EffectTableRestoreCallbacks restoreCallbacks{
        &restoreState,
        ValidateRestoreLifecycle,
        RegisterRestoredEffect,
    };
    const archive::EffectTableRestoreResult restored =
        archive::RestoreEffectTableNoReport(
            &reader,
            &identityToken,
            generation,
            restoreCallbacks);
    CHECK(restored.status == archive::EffectTableRestoreStatus::Success);
    CHECK(restored.entryCount == restoredEntries.size());
    CHECK(restoreState.registerCalls == restoredEntries.size());
    CHECK(!restoreState.mismatch);
    for (std::size_t index = 0; index < restoredEntries.size(); ++index)
    {
        std::uint32_t key = 0;
        const void *definition = nullptr;
        CHECK(archive::EffectTableRestoreGetEntry(
            restored.lease, index, &key, &definition));
        CHECK(key
            == static_cast<std::uint32_t>(restoredEntries[index].key));
        CHECK(definition == &restoreState.definitions[index]);
    }
    CHECK(archive::ReleaseEffectTableRestore(restored.lease)
        == archive::EffectTableRestoreStatus::Success);
    CloseReader(reader);
    storage.CheckCanaries();
}

void TestMemFileRoundTrips()
{
    const std::vector<EntrySpec> entries{
        {"fx/roundtrip-a", 0x78563412u},
        {"world\\roundtrip-b", 2u},
        {std::string(
             archive::EFFECT_TABLE_RESTORE_NAME_CAPACITY - 1u,
             'r'),
         0xFFFFFFFFu},
    };
    const std::vector<EntrySpec> exactDuplicates{
        {"fx/repeated", 0x1234u},
        {"fx/repeated", 0x1234u},
        {"fx/unique", 0x5678u},
    };
    RunMemFileRoundTrip(entries, false);
    RunMemFileRoundTrip(entries, true);
    RunMemFileRoundTrip(exactDuplicates, false);
    RunMemFileRoundTrip(exactDuplicates, true);
}

struct ConstrainedStackState
{
    bool completed = false;
};

void RunConstrainedStackChecks(ConstrainedStackState *const state)
{
    // Keep all large fixtures on the heap.  In particular, the opaque save
    // snapshot is roughly 68 KiB and cannot fit on this worker's production
    // stack reservation.
    const std::vector<EntrySpec> capacityEntries =
        MakeCapacityEntries(archive::EFFECT_TABLE_RESTORE_CAPACITY);
    CheckSuccessfulWrite(capacityEntries);
    RunMemFileRoundTrip(capacityEntries, false);
    RunMemFileRoundTrip(capacityEntries, true);
    state->completed = true;
}

#if defined(_WIN32)
DWORD WINAPI ConstrainedStackThread(void *const context)
{
    RunConstrainedStackChecks(
        static_cast<ConstrainedStackState *>(context));
    return 0;
}
#elif defined(__APPLE__) || defined(__unix__)
void *ConstrainedStackThread(void *const context)
{
    RunConstrainedStackChecks(
        static_cast<ConstrainedStackState *>(context));
    return nullptr;
}
#endif

void TestConstrainedStackWorker()
{
    // Sanitizer runtimes add substantial instrumentation frames.  Preserve a
    // bounded test there with a 1 MiB ceiling; ordinary builds use the engine's
    // 64 KiB worker-thread budget exactly.
#if defined(KISAK_SAVE_TEST_STACK_SANITIZER)
    constexpr std::size_t stackSize = 1024u * 1024u;
#else
    constexpr std::size_t stackSize = 64u * 1024u;
#endif
    ConstrainedStackState state{};

#if defined(_WIN32)
    HANDLE const thread = CreateThread(
        nullptr,
        stackSize,
        ConstrainedStackThread,
        &state,
        STACK_SIZE_PARAM_IS_A_RESERVATION,
        nullptr);
    CHECK(thread != nullptr);
    if (!thread)
        return;
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    CHECK(CloseHandle(thread) != 0);
#elif defined(__APPLE__) || defined(__unix__)
    pthread_attr_t attributes{};
    int result = pthread_attr_init(&attributes);
    CHECK(result == 0);
    if (result != 0)
        return;

    std::size_t requestedStackSize = stackSize;
    const std::size_t minimumStackSize =
        static_cast<std::size_t>(PTHREAD_STACK_MIN);
    if (requestedStackSize < minimumStackSize)
        requestedStackSize = minimumStackSize;
    result = pthread_attr_setstacksize(
        &attributes, requestedStackSize);
    CHECK(result == 0);
    if (result != 0)
    {
        CHECK(pthread_attr_destroy(&attributes) == 0);
        return;
    }

    pthread_t thread{};
    result = pthread_create(
        &thread, &attributes, ConstrainedStackThread, &state);
    CHECK(pthread_attr_destroy(&attributes) == 0);
    CHECK(result == 0);
    if (result != 0)
        return;
    CHECK(pthread_join(thread, nullptr) == 0);
#else
    RunConstrainedStackChecks(&state);
#endif

    CHECK(state.completed);
    CHECK(!archive::EffectTableRestoreLeaseIsActive());
}
} // namespace

void MyAssertHandler(
    const char *filename,
    int line,
    int type,
    const char *format,
    ...)
{
    (void)filename;
    (void)line;
    (void)type;
    (void)format;
    unexpectedReports.fetch_add(1, std::memory_order_relaxed);
}

void QDECL Com_Printf(const int channel, const char *format, ...)
{
    (void)channel;
    (void)format;
    unexpectedReports.fetch_add(1, std::memory_order_relaxed);
}

void QDECL Com_Error(const errorParm_t code, const char *format, ...)
{
    (void)code;
    (void)format;
    unexpectedReports.fetch_add(1, std::memory_order_relaxed);
}

char *QDECL va(const char *format, ...)
{
    static char result[1]{};
    (void)format;
    return result;
}

bool __cdecl Sys_IsMainThread()
{
    return std::this_thread::get_id() == mainThreadId;
}

bool __cdecl Sys_IsRenderThread()
{
    return false;
}

bool __cdecl Sys_IsDatabaseThread()
{
    return false;
}

int main()
{
    mainThreadId = std::this_thread::get_id();
    TestConstructionAndCanaries();
    TestExactLogicalBytes();
    TestCaptureFailures();
    TestInvalidNames();
    TestDuplicatePolicies();
    TestInvalidStateAndMissingWriter();
    TestWriterFailures();
    TestWriterReentry();
    TestMemFileRoundTrips();
    TestConstrainedStackWorker();
    CHECK(unexpectedReports.load(std::memory_order_relaxed) == 0);
    CHECK(!archive::EffectTableRestoreLeaseIsActive());

    const int failureCount = failures.load(std::memory_order_relaxed);
    if (failureCount != 0)
    {
        std::fprintf(
            stderr,
            "%d effect-table save test(s) failed\n",
            failureCount);
        return 1;
    }
    std::puts("Effect-table save snapshot tests passed");
    return 0;
}
