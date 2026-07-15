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
    std::uint32_t key;
};

struct FakeDefinition
{
    std::uint32_t id = 0;
};

struct CallbackState
{
    const void *expectedIdentity = nullptr;
    std::uint32_t expectedGeneration = 0;
    bool lifecycleValid = true;
    bool lifecycleMismatch = false;
    std::size_t validateCalls = 0;

    std::vector<std::string> expectedNames;
    std::vector<FakeDefinition> definitions;
    std::size_t registerCalls = 0;
    std::size_t failAt = std::numeric_limits<std::size_t>::max();
    bool nameMismatch = false;

    bool reenterOnFirstRegistration = false;
    bool abandonOnFirstRegistration = false;
    bool clobberArchiveOnFirstRegistration = false;
    MemoryFile *reentryReader = nullptr;
    std::vector<std::uint8_t> *archiveToClobber = nullptr;
    archive::EffectTableRestoreStatus nestedStatus =
        archive::EffectTableRestoreStatus::UnsafeFailure;
    bool nestedReadCursorUnchanged = false;
};

bool ValidateLifecycle(
    void *context,
    const void *identity,
    std::uint32_t lifecycleGeneration) noexcept;

const void *RegisterEffect(void *context, const char *name) noexcept;

archive::EffectTableRestoreCallbacks MakeCallbacks(
    CallbackState *const state) noexcept
{
    archive::EffectTableRestoreCallbacks callbacks{};
    callbacks.context = state;
    callbacks.validateLifecycle = ValidateLifecycle;
    callbacks.registerEffect = RegisterEffect;
    return callbacks;
}

bool ValidateLifecycle(
    void *const context,
    const void *const identity,
    const std::uint32_t lifecycleGeneration) noexcept
{
    auto *const state = static_cast<CallbackState *>(context);
    ++state->validateCalls;
    if (identity != state->expectedIdentity
        || lifecycleGeneration != state->expectedGeneration)
    {
        state->lifecycleMismatch = true;
        return false;
    }
    return state->lifecycleValid;
}

const void *RegisterEffect(
    void *const context,
    const char *const name) noexcept
{
    auto *const state = static_cast<CallbackState *>(context);
    const std::size_t attempt = state->registerCalls;
    ++state->registerCalls;

    if (!name || attempt >= state->expectedNames.size()
        || state->expectedNames[attempt] != name)
    {
        state->nameMismatch = true;
    }

    if (attempt == 0 && state->reenterOnFirstRegistration)
    {
        const int cursorBefore = state->reentryReader
            ? state->reentryReader->bytesUsed
            : -1;
        const archive::EffectTableRestoreResult nested =
            archive::RestoreEffectTableNoReport(
                state->reentryReader,
                state->expectedIdentity,
                state->expectedGeneration,
                MakeCallbacks(state));
        state->nestedStatus = nested.status;
        state->nestedReadCursorUnchanged = state->reentryReader
            && state->reentryReader->bytesUsed == cursorBefore;
    }

    if (attempt == 0 && state->clobberArchiveOnFirstRegistration
        && state->archiveToClobber)
    {
        std::fill(
            state->archiveToClobber->begin(),
            state->archiveToClobber->end(),
            static_cast<std::uint8_t>(0));
    }

    if (attempt == 0 && state->abandonOnFirstRegistration)
        archive::AbandonCurrentThreadEffectTableRestoreForError();

    if (attempt == state->failAt || attempt >= state->definitions.size())
        return nullptr;
    return &state->definitions[attempt];
}

void AppendCString(
    std::vector<std::uint8_t> &output,
    const std::string &value)
{
    output.insert(output.end(), value.begin(), value.end());
    output.push_back(0);
}

void AppendLittleEndianU32(
    std::vector<std::uint8_t> &output,
    const std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8u));
    output.push_back(static_cast<std::uint8_t>(value >> 16u));
    output.push_back(static_cast<std::uint8_t>(value >> 24u));
}

std::vector<std::uint8_t> MakeLogicalTable(
    const std::vector<EntrySpec> &entries,
    const bool includeTerminator = true,
    const std::vector<std::uint8_t> &tail = {})
{
    std::vector<std::uint8_t> logical;
    for (const EntrySpec &entry : entries)
    {
        AppendCString(logical, entry.name);
        AppendLittleEndianU32(logical, entry.key);
    }
    if (includeTerminator)
        logical.push_back(0);
    logical.insert(logical.end(), tail.begin(), tail.end());
    return logical;
}

std::vector<std::uint8_t> WriteArchive(
    const std::vector<std::uint8_t> &payload,
    const bool compress)
{
    CHECK(payload.size()
        <= (static_cast<std::size_t>(INT_MAX) - 4096u) / 3u);
    if (payload.size()
        > (static_cast<std::size_t>(INT_MAX) - 4096u) / 3u)
    {
        return {};
    }

    const std::size_t capacity = payload.size() * 3u + 4096u;
    std::vector<std::uint8_t> archive(capacity);
    MemoryFile writer{};
    MemFile_InitForWriting(
        &writer,
        static_cast<int>(archive.size()),
        archive.data(),
        false,
        compress);
    if (!payload.empty())
    {
        MemFile_WriteData(
            &writer,
            static_cast<int>(payload.size()),
            payload.data());
    }
    MemFile_StartSegment(&writer, -1);
    CHECK(!writer.memoryOverflow);
    CHECK(writer.bufferSize >= 0);
    if (writer.bufferSize >= 0)
        archive.resize(static_cast<std::size_t>(writer.bufferSize));
    MemFile_Shutdown(&writer);
    return archive;
}

void InitReader(
    MemoryFile &reader,
    std::vector<std::uint8_t> &archiveBytes,
    const bool compress)
{
    CHECK(!archiveBytes.empty());
    CHECK(archiveBytes.size() <= static_cast<std::size_t>(INT_MAX));
    if (archiveBytes.empty()
        || archiveBytes.size() > static_cast<std::size_t>(INT_MAX))
    {
        return;
    }
    MemFile_InitForReading(
        &reader,
        static_cast<int>(archiveBytes.size()),
        archiveBytes.data(),
        compress);
}

void CloseReader(MemoryFile &reader)
{
    if (!reader.memoryOverflow && reader.segmentIndex >= 0)
        MemFile_MoveToSegment(&reader, -1);
    MemFile_Shutdown(&reader);
}

CallbackState MakeState(
    const void *const identity,
    const std::uint32_t generation,
    const std::vector<EntrySpec> &expectedEntries)
{
    CallbackState state{};
    state.expectedIdentity = identity;
    state.expectedGeneration = generation;
    state.expectedNames.reserve(expectedEntries.size());
    state.definitions.resize(expectedEntries.size());
    for (std::size_t index = 0; index < expectedEntries.size(); ++index)
    {
        state.expectedNames.push_back(expectedEntries[index].name);
        state.definitions[index].id = static_cast<std::uint32_t>(index + 1u);
    }
    return state;
}

void CheckEmptyLease(const archive::EffectTableRestoreLease &lease)
{
    CHECK(lease.identity == nullptr);
    CHECK(lease.lifecycleGeneration == 0);
    CHECK(lease.serial == 0);
    CHECK(lease.ownerCookie == nullptr);
}

void CheckFailureResult(
    const archive::EffectTableRestoreResult &result,
    const archive::EffectTableRestoreStatus expectedStatus)
{
    CHECK(result.status == expectedStatus);
    CHECK(result.entryCount == 0);
    CheckEmptyLease(result.lease);
    CHECK(!archive::EffectTableRestoreLeaseIsActive());
}

void CheckLeaseEntries(
    const archive::EffectTableRestoreResult &result,
    const std::vector<EntrySpec> &expectedEntries,
    CallbackState &state)
{
    CHECK(result.entryCount == expectedEntries.size());
    for (std::size_t index = 0; index < expectedEntries.size(); ++index)
    {
        archive::EffectDefinitionKey32 key{0xFFFFFFFFu};
        const void *definition = reinterpret_cast<const void *>(
            static_cast<std::uintptr_t>(1));
        CHECK(archive::EffectTableRestoreGetEntry(
            result.lease, index, &key, &definition));
        CHECK(key.value == expectedEntries[index].key);
        CHECK(definition == &state.definitions[index]);
        CHECK(archive::EffectTableRestoreFind(
            result.lease,
            archive::EffectDefinitionKey32{expectedEntries[index].key})
            == &state.definitions[index]);
    }

    archive::EffectDefinitionKey32 sentinelKey{0xA5A5A5A5u};
    const void *sentinelDefinition = reinterpret_cast<const void *>(
        static_cast<std::uintptr_t>(3));
    CHECK(!archive::EffectTableRestoreGetEntry(
        result.lease,
        expectedEntries.size(),
        &sentinelKey,
        &sentinelDefinition));
    CHECK(sentinelKey.value == 0xA5A5A5A5u);
    CHECK(sentinelDefinition == reinterpret_cast<const void *>(
        static_cast<std::uintptr_t>(3)));
    CHECK(!archive::EffectTableRestoreGetEntry(
        result.lease, 0, nullptr, &sentinelDefinition));
    CHECK(!archive::EffectTableRestoreGetEntry(
        result.lease, 0, &sentinelKey, nullptr));
    CHECK(archive::EffectTableRestoreFind(
        result.lease, archive::EffectDefinitionKey32{0}) == nullptr);
}

void RunSuccessfulRestore(
    const std::vector<EntrySpec> &encodedEntries,
    const std::vector<EntrySpec> &expectedEntries,
    const std::vector<std::uint8_t> &tail,
    const bool compress)
{
    const std::vector<std::uint8_t> logical =
        MakeLogicalTable(encodedEntries, true, tail);
    std::vector<std::uint8_t> archiveBytes =
        WriteArchive(logical, compress);
    MemoryFile reader{};
    InitReader(reader, archiveBytes, compress);

    int identityToken = 0;
    constexpr std::uint32_t generation = 0x31415926u;
    CallbackState state =
        MakeState(&identityToken, generation, expectedEntries);
    const archive::EffectTableRestoreResult result =
        archive::RestoreEffectTableNoReport(
            &reader,
            &identityToken,
            generation,
            MakeCallbacks(&state));

    CHECK(result.status == archive::EffectTableRestoreStatus::Success);
    CHECK(archive::EffectTableRestoreLeaseIsActive());
    CHECK(!state.lifecycleMismatch);
    CHECK(!state.nameMismatch);
    CHECK(state.registerCalls == expectedEntries.size());
    CHECK(state.validateCalls >= 3u);
    CheckLeaseEntries(result, expectedEntries, state);

    if (!tail.empty())
    {
        std::vector<std::uint8_t> observedTail(tail.size(), 0xEEu);
        CHECK(MemFile_TryReadDataNoReport(
            &reader,
            static_cast<int>(observedTail.size()),
            observedTail.data()) == MemFileReadStatus::Success);
        CHECK(observedTail == tail);
    }

    CHECK(archive::ReleaseEffectTableRestore(result.lease)
        == archive::EffectTableRestoreStatus::Success);
    CHECK(!archive::EffectTableRestoreLeaseIsActive());
    archive::EffectDefinitionKey32 key{};
    const void *definition = nullptr;
    CHECK(!archive::EffectTableRestoreGetEntry(
        result.lease, 0, &key, &definition));
    CHECK(archive::EffectTableRestoreFind(
        result.lease, archive::EffectDefinitionKey32{1}) == nullptr);
    CloseReader(reader);
}

void RunMalformedRestore(
    const std::vector<std::uint8_t> &logical,
    const archive::EffectTableRestoreStatus expectedStatus,
    const bool expectOverflow,
    const bool compress)
{
    std::vector<std::uint8_t> archiveBytes =
        WriteArchive(logical, compress);
    MemoryFile reader{};
    InitReader(reader, archiveBytes, compress);

    int identityToken = 0;
    constexpr std::uint32_t generation = 29;
    const std::vector<EntrySpec> noRegistrations;
    CallbackState state =
        MakeState(&identityToken, generation, noRegistrations);
    const archive::EffectTableRestoreResult result =
        archive::RestoreEffectTableNoReport(
            &reader,
            &identityToken,
            generation,
            MakeCallbacks(&state));

    CheckFailureResult(result, expectedStatus);
    CHECK(state.registerCalls == 0);
    CHECK(!state.lifecycleMismatch);
    CHECK(reader.memoryOverflow == expectOverflow);
    CloseReader(reader);
}

void TestValidTables()
{
    const std::vector<std::uint8_t> tail{
        0x91u, 0x00u, 0xFEu, 0x44u, 0x7Au,
    };
    const std::vector<EntrySpec> empty;
    const std::vector<EntrySpec> one{{"fx/smoke", 7u}};
    const std::vector<EntrySpec> multiple{
        {"fx/smoke", 0x78563412u},
        {"weapons/rifle/muzzle", 2u},
        {"world\\dust", 0x80000000u},
    };
    const std::vector<EntrySpec> maximumName{{
        std::string(
            archive::EFFECT_TABLE_RESTORE_NAME_CAPACITY - 1u, 'n'),
        0x01020304u,
    }};

    for (const bool compress : {false, true})
    {
        RunSuccessfulRestore(empty, empty, tail, compress);
        RunSuccessfulRestore(one, one, tail, compress);
        RunSuccessfulRestore(multiple, multiple, tail, compress);
        RunSuccessfulRestore(
            maximumName, maximumName, tail, compress);
    }
}

std::vector<EntrySpec> MakeCapacityTable(const std::size_t count)
{
    std::vector<EntrySpec> entries;
    entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        entries.push_back({
            "fx/e" + std::to_string(index),
            static_cast<std::uint32_t>(index + 1u),
        });
    }
    return entries;
}

void TestMaximumCapacity()
{
    const std::vector<EntrySpec> entries =
        MakeCapacityTable(archive::EFFECT_TABLE_RESTORE_CAPACITY);
    const std::vector<std::uint8_t> tail{0xC3u, 0x5Au};
    for (const bool compress : {false, true})
        RunSuccessfulRestore(entries, entries, tail, compress);
}

void TestMalformedTables()
{
    std::vector<std::vector<std::uint8_t>> truncatedKeys;
    truncatedKeys.reserve(sizeof(std::uint32_t));
    constexpr std::array<std::uint8_t, sizeof(std::uint32_t)> keyBytes{
        0x78u, 0x56u, 0x34u, 0x12u,
    };
    for (std::size_t byteCount = 0; byteCount < keyBytes.size(); ++byteCount)
    {
        std::vector<std::uint8_t> truncated =
            MakeLogicalTable({{"fx/complete", 0x10203040u}}, false);
        AppendCString(truncated, "fx/late");
        truncated.insert(
            truncated.end(),
            keyBytes.begin(),
            keyBytes.begin() + static_cast<std::ptrdiff_t>(byteCount));
        truncatedKeys.push_back(std::move(truncated));
    }

    const std::vector<EntrySpec> one{{"fx/a", 1u}};
    const std::vector<std::uint8_t> missingTerminator =
        MakeLogicalTable(one, false);

    std::vector<std::uint8_t> longName;
    AppendCString(
        longName,
        std::string(archive::EFFECT_TABLE_RESTORE_NAME_CAPACITY, 'x'));
    AppendLittleEndianU32(longName, 1u);
    longName.push_back(0);

    std::vector<std::uint8_t> hugeName;
    AppendCString(hugeName, std::string(1025u, 'x'));
    AppendLittleEndianU32(hugeName, 1u);
    hugeName.push_back(0);

    const std::vector<std::uint8_t> zeroKey =
        MakeLogicalTable({{"fx/zero", 0u}});
    const std::vector<std::uint8_t> conflict = MakeLogicalTable({
        {"fx/first", 9u},
        {"fx/second", 9u},
    });
    const std::vector<std::uint8_t> overCapacity = MakeLogicalTable(
        MakeCapacityTable(archive::EFFECT_TABLE_RESTORE_CAPACITY + 1u));

    for (const bool compress : {false, true})
    {
        RunMalformedRestore(
            {},
            archive::EffectTableRestoreStatus::TruncatedInput,
            true,
            compress);
        for (const std::vector<std::uint8_t> &truncatedKey : truncatedKeys)
        {
            RunMalformedRestore(
                truncatedKey,
                archive::EffectTableRestoreStatus::TruncatedInput,
                true,
                compress);
        }
        RunMalformedRestore(
            missingTerminator,
            archive::EffectTableRestoreStatus::TruncatedInput,
            true,
            compress);
        RunMalformedRestore(
            longName,
            archive::EffectTableRestoreStatus::NameTooLong,
            false,
            compress);
        RunMalformedRestore(
            hugeName,
            archive::EffectTableRestoreStatus::NameTooLong,
            false,
            compress);
        RunMalformedRestore(
            zeroKey,
            archive::EffectTableRestoreStatus::InvalidKey,
            false,
            compress);
        RunMalformedRestore(
            conflict,
            archive::EffectTableRestoreStatus::ConflictingDuplicate,
            false,
            compress);
        RunMalformedRestore(
            overCapacity,
            archive::EffectTableRestoreStatus::CapacityExceeded,
            false,
            compress);
    }
}

void TestInvalidNames()
{
    std::string controlName{"fx/"};
    controlName.push_back(static_cast<char>(0x1F));
    controlName.push_back('x');
    std::string deleteName{"fx/"};
    deleteName.push_back(static_cast<char>(0x7F));
    deleteName.push_back('x');
    std::string comSuperscript{"COM"};
    comSuperscript.push_back(static_cast<char>(0xB9));
    std::string lptSuperscriptUtf8{"LPT"};
    lptSuperscriptUtf8.push_back(static_cast<char>(0xC2));
    lptSuperscriptUtf8.push_back(static_cast<char>(0xB2));

    const std::vector<std::string> invalidNames{
        "/absolute",
        "\\absolute",
        "fx//smoke",
        "fx\\\\smoke",
        "./smoke",
        "../smoke",
        "fx/./smoke",
        "fx/../smoke",
        "fx.",
        "fx ",
        "fx./smoke",
        "fx /smoke",
        "fx/smoke.",
        "fx/smoke ",
        "C:smoke",
        "CON",
        "con.efx",
        "fx/NUL",
        "fx/aux.txt",
        "PRN/smoke",
        "COM1",
        "com9.bin",
        "LPT1",
        "lpt9.foo",
        "fx/CON .efx",
        "CONIN$",
        "conout$.log",
        "CLOCK$",
        "fx/quote\"name",
        "fx/less<name",
        "fx/greater>name",
        "fx/pipe|name",
        "fx/question?name",
        "fx/star*name",
        comSuperscript,
        lptSuperscriptUtf8,
        controlName,
        deleteName,
    };

    CHECK(!archive::EffectTableRestoreNameIsValid(nullptr));
    CHECK(!archive::EffectTableRestoreNameIsValid(""));
    CHECK(archive::EffectTableRestoreNameIsValid("fx/smoke"));
    CHECK(archive::EffectTableRestoreNameIsValid("fx\\smoke"));
    CHECK(archive::EffectTableRestoreNameIsValid("console"));
    CHECK(archive::EffectTableRestoreNameIsValid("null"));
    CHECK(archive::EffectTableRestoreNameIsValid("com0"));
    CHECK(archive::EffectTableRestoreNameIsValid("com10"));
    CHECK(archive::EffectTableRestoreNameIsValid("lpt0"));
    CHECK(archive::EffectTableRestoreNameIsValid("lpt10"));
    CHECK(archive::EffectTableRestoreNameIsValid(
        std::string(
            archive::EFFECT_TABLE_RESTORE_NAME_CAPACITY - 1u,
            'v').c_str()));
    CHECK(!archive::EffectTableRestoreNameIsValid(
        std::string(
            archive::EFFECT_TABLE_RESTORE_NAME_CAPACITY,
            'v').c_str()));

    for (const std::string &name : invalidNames)
    {
        CHECK(!archive::EffectTableRestoreNameIsValid(name.c_str()));
        const std::vector<std::uint8_t> logical =
            MakeLogicalTable({{name, 1u}});
        for (const bool compress : {false, true})
        {
            RunMalformedRestore(
                logical,
                archive::EffectTableRestoreStatus::InvalidName,
                false,
                compress);
        }
    }
}

void TestDuplicatePolicies()
{
    const std::vector<EntrySpec> identicalEncoded{
        {"fx/repeated", 17u},
        {"fx/repeated", 17u},
        {"fx/repeated", 17u},
    };
    const std::vector<EntrySpec> identicalExpected{
        {"fx/repeated", 17u},
    };
    const std::vector<EntrySpec> sameNameDifferentKeys{
        {"fx/shared", 30u},
        {"fx/shared", 31u},
    };
    for (const bool compress : {false, true})
    {
        RunSuccessfulRestore(
            identicalEncoded, identicalExpected, {}, compress);
        RunSuccessfulRestore(
            sameNameDifferentKeys,
            sameNameDifferentKeys,
            {},
            compress);
    }
}

void RunRegistrationFailure(
    const std::size_t failAt,
    const bool compress)
{
    const std::vector<EntrySpec> entries{
        {"fx/first", 101u},
        {"fx/middle", 102u},
        {"fx/last", 103u},
    };
    std::vector<std::uint8_t> archiveBytes =
        WriteArchive(MakeLogicalTable(entries), compress);
    MemoryFile reader{};
    InitReader(reader, archiveBytes, compress);

    int identityToken = 0;
    constexpr std::uint32_t generation = 42;
    CallbackState state = MakeState(&identityToken, generation, entries);
    state.failAt = failAt;
    const archive::EffectTableRestoreResult result =
        archive::RestoreEffectTableNoReport(
            &reader,
            &identityToken,
            generation,
            MakeCallbacks(&state));
    CheckFailureResult(
        result, archive::EffectTableRestoreStatus::RegistrationFailed);
    CHECK(state.registerCalls == failAt + 1u);
    CHECK(!state.nameMismatch);
    CloseReader(reader);
}

void TestRegistrationFailureAtomicity()
{
    for (const bool compress : {false, true})
    {
        RunRegistrationFailure(0, compress);
        RunRegistrationFailure(1, compress);
        RunRegistrationFailure(2, compress);
        const std::vector<EntrySpec> reuse{{"fx/reuse", 901u}};
        RunSuccessfulRestore(reuse, reuse, {}, compress);
    }
}

void TestInvalidArgumentsAndState()
{
    int identityToken = 0;
    constexpr std::uint32_t generation = 8;
    const std::vector<EntrySpec> entries{{"fx/a", 1u}};
    CallbackState state = MakeState(&identityToken, generation, entries);
    MemoryFile invalidReader{};
    const archive::EffectTableRestoreCallbacks callbacks =
        MakeCallbacks(&state);

    CheckFailureResult(
        archive::RestoreEffectTableNoReport(
            nullptr, &identityToken, generation, callbacks),
        archive::EffectTableRestoreStatus::InvalidArgument);
    CheckFailureResult(
        archive::RestoreEffectTableNoReport(
            &invalidReader, nullptr, generation, callbacks),
        archive::EffectTableRestoreStatus::InvalidArgument);

    archive::EffectTableRestoreCallbacks missingValidate = callbacks;
    missingValidate.validateLifecycle = nullptr;
    CheckFailureResult(
        archive::RestoreEffectTableNoReport(
            &invalidReader,
            &identityToken,
            generation,
            missingValidate),
        archive::EffectTableRestoreStatus::InvalidArgument);
    archive::EffectTableRestoreCallbacks missingRegister = callbacks;
    missingRegister.registerEffect = nullptr;
    CheckFailureResult(
        archive::RestoreEffectTableNoReport(
            &invalidReader,
            &identityToken,
            generation,
            missingRegister),
        archive::EffectTableRestoreStatus::InvalidArgument);

    const archive::EffectTableRestoreResult invalidState =
        archive::RestoreEffectTableNoReport(
            &invalidReader,
            &identityToken,
            generation,
            callbacks);
    CheckFailureResult(
        invalidState, archive::EffectTableRestoreStatus::InvalidState);
    CHECK(state.registerCalls == 0);
}

void TestLifecycleRejectionAndRelease()
{
    const std::vector<EntrySpec> entries{{"fx/lifecycle", 72u}};
    std::vector<std::uint8_t> archiveBytes =
        WriteArchive(MakeLogicalTable(entries), false);
    MemoryFile reader{};
    InitReader(reader, archiveBytes, false);
    const int cursorBefore = reader.bytesUsed;

    int identityToken = 0;
    constexpr std::uint32_t generation = 73;
    CallbackState state = MakeState(&identityToken, generation, entries);
    state.lifecycleValid = false;
    const archive::EffectTableRestoreResult rejected =
        archive::RestoreEffectTableNoReport(
            &reader,
            &identityToken,
            generation,
            MakeCallbacks(&state));
    CheckFailureResult(
        rejected, archive::EffectTableRestoreStatus::LifecycleChanged);
    CHECK(reader.bytesUsed == cursorBefore);
    CHECK(state.registerCalls == 0);

    state.lifecycleValid = true;
    const archive::EffectTableRestoreResult accepted =
        archive::RestoreEffectTableNoReport(
            &reader,
            &identityToken,
            generation,
            MakeCallbacks(&state));
    CHECK(accepted.status == archive::EffectTableRestoreStatus::Success);
    state.lifecycleValid = false;
    CHECK(archive::ReleaseEffectTableRestore(accepted.lease)
        == archive::EffectTableRestoreStatus::LifecycleChanged);
    CHECK(!archive::EffectTableRestoreLeaseIsActive());
    CloseReader(reader);
}

void TestReentryIsBusyBeforeRead()
{
    const std::vector<EntrySpec> entries{
        {"fx/outer-a", 81u},
        {"fx/outer-b", 82u},
    };
    for (const bool compress : {false, true})
    {
        std::vector<std::uint8_t> archiveBytes =
            WriteArchive(MakeLogicalTable(entries), compress);
        MemoryFile reader{};
        InitReader(reader, archiveBytes, compress);

        int identityToken = 0;
        constexpr std::uint32_t generation = 83;
        CallbackState state = MakeState(&identityToken, generation, entries);
        state.reenterOnFirstRegistration = true;
        state.reentryReader = &reader;
        const archive::EffectTableRestoreResult result =
            archive::RestoreEffectTableNoReport(
                &reader,
                &identityToken,
                generation,
                MakeCallbacks(&state));
        CHECK(result.status == archive::EffectTableRestoreStatus::Success);
        CHECK(state.nestedStatus == archive::EffectTableRestoreStatus::Busy);
        CHECK(state.nestedReadCursorUnchanged);
        CHECK(state.registerCalls == entries.size());
        CHECK(archive::ReleaseEffectTableRestore(result.lease)
            == archive::EffectTableRestoreStatus::Success);
        CloseReader(reader);
    }
}

void TestCallbackAbandonment()
{
    const std::vector<EntrySpec> entries{{"fx/abandon", 91u}};
    for (const bool compress : {false, true})
    {
        std::vector<std::uint8_t> archiveBytes =
            WriteArchive(MakeLogicalTable(entries), compress);
        MemoryFile reader{};
        InitReader(reader, archiveBytes, compress);

        int identityToken = 0;
        constexpr std::uint32_t generation = 92;
        CallbackState state = MakeState(&identityToken, generation, entries);
        state.abandonOnFirstRegistration = true;
        const archive::EffectTableRestoreResult result =
            archive::RestoreEffectTableNoReport(
                &reader,
                &identityToken,
                generation,
                MakeCallbacks(&state));
        CheckFailureResult(
            result, archive::EffectTableRestoreStatus::OwnerMismatch);
        CHECK(state.registerCalls == 1u);
        CloseReader(reader);

        RunSuccessfulRestore(entries, entries, {}, compress);
    }
}

void TestRegistrationUsesStagedNames()
{
    const std::vector<EntrySpec> entries{
        {"fx/staged-a", 201u},
        {"fx/staged-b", 202u},
        {"fx/staged-c", 203u},
    };
    std::vector<std::uint8_t> archiveBytes =
        WriteArchive(MakeLogicalTable(entries), false);
    MemoryFile reader{};
    InitReader(reader, archiveBytes, false);

    int identityToken = 0;
    constexpr std::uint32_t generation = 204;
    CallbackState state = MakeState(&identityToken, generation, entries);
    state.clobberArchiveOnFirstRegistration = true;
    state.archiveToClobber = &archiveBytes;
    const archive::EffectTableRestoreResult result =
        archive::RestoreEffectTableNoReport(
            &reader,
            &identityToken,
            generation,
            MakeCallbacks(&state));
    CHECK(result.status == archive::EffectTableRestoreStatus::Success);
    CHECK(state.registerCalls == entries.size());
    CHECK(!state.nameMismatch);
    CheckLeaseEntries(result, entries, state);
    CHECK(archive::ReleaseEffectTableRestore(result.lease)
        == archive::EffectTableRestoreStatus::Success);
    CloseReader(reader);
}

void TestBusyStaleLeaseAndReuse()
{
    const std::vector<EntrySpec> firstEntries{{"fx/old", 301u}};
    std::vector<std::uint8_t> firstArchive =
        WriteArchive(MakeLogicalTable(firstEntries), false);
    MemoryFile firstReader{};
    InitReader(firstReader, firstArchive, false);
    int firstIdentity = 0;
    CallbackState firstState = MakeState(&firstIdentity, 302u, firstEntries);
    const archive::EffectTableRestoreResult first =
        archive::RestoreEffectTableNoReport(
            &firstReader,
            &firstIdentity,
            302u,
            MakeCallbacks(&firstState));
    CHECK(first.status == archive::EffectTableRestoreStatus::Success);
    CloseReader(firstReader);

    const std::vector<EntrySpec> secondEntries{{"fx/new", 401u}};
    std::vector<std::uint8_t> secondArchive =
        WriteArchive(MakeLogicalTable(secondEntries), false);
    MemoryFile secondReader{};
    InitReader(secondReader, secondArchive, false);
    const int cursorBefore = secondReader.bytesUsed;
    int secondIdentity = 0;
    CallbackState secondState =
        MakeState(&secondIdentity, 402u, secondEntries);
    const archive::EffectTableRestoreResult busy =
        archive::RestoreEffectTableNoReport(
            &secondReader,
            &secondIdentity,
            402u,
            MakeCallbacks(&secondState));
    CHECK(busy.status == archive::EffectTableRestoreStatus::Busy);
    CheckEmptyLease(busy.lease);
    CHECK(secondReader.bytesUsed == cursorBefore);
    CHECK(secondState.registerCalls == 0);
    CHECK(archive::EffectTableRestoreLeaseIsActive());

    CHECK(archive::ReleaseEffectTableRestore(first.lease)
        == archive::EffectTableRestoreStatus::Success);
    const archive::EffectTableRestoreResult second =
        archive::RestoreEffectTableNoReport(
            &secondReader,
            &secondIdentity,
            402u,
            MakeCallbacks(&secondState));
    CHECK(second.status == archive::EffectTableRestoreStatus::Success);
    CHECK(archive::EffectTableRestoreFind(
        second.lease, archive::EffectDefinitionKey32{301u}) == nullptr);
    CHECK(archive::EffectTableRestoreFind(
        second.lease, archive::EffectDefinitionKey32{401u})
        == &secondState.definitions[0]);

    CHECK(archive::ReleaseEffectTableRestore(first.lease)
        == archive::EffectTableRestoreStatus::OwnerMismatch);
    CHECK(archive::EffectTableRestoreLeaseIsActive());
    CHECK(archive::EffectTableRestoreFind(
        second.lease, archive::EffectDefinitionKey32{401u})
        == &secondState.definitions[0]);
    CHECK(archive::ReleaseEffectTableRestore(second.lease)
        == archive::EffectTableRestoreStatus::Success);
    CHECK(!archive::EffectTableRestoreLeaseIsActive());
    CloseReader(secondReader);
}

void TestForeignOwnerAndContention()
{
    const std::vector<EntrySpec> entries{{"fx/held", 501u}};
    std::vector<std::uint8_t> archiveBytes =
        WriteArchive(MakeLogicalTable(entries), false);
    MemoryFile reader{};
    InitReader(reader, archiveBytes, false);
    int identityToken = 0;
    CallbackState state = MakeState(&identityToken, 502u, entries);
    const archive::EffectTableRestoreResult held =
        archive::RestoreEffectTableNoReport(
            &reader,
            &identityToken,
            502u,
            MakeCallbacks(&state));
    CHECK(held.status == archive::EffectTableRestoreStatus::Success);
    CloseReader(reader);

    constexpr std::size_t workerCount = 4;
    constexpr std::size_t iterations = 200;
    std::atomic<std::size_t> ready{0};
    std::atomic<bool> go{false};
    std::vector<int> workerResults(workerCount, 0);
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (std::size_t worker = 0; worker < workerCount; ++worker)
    {
        workers.emplace_back([&, worker]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire))
                std::this_thread::yield();

            bool okay = true;
            for (std::size_t iteration = 0; iteration < iterations; ++iteration)
            {
                okay = okay
                    && archive::ReleaseEffectTableRestore(held.lease)
                        == archive::EffectTableRestoreStatus::OwnerMismatch;
                archive::AbandonCurrentThreadEffectTableRestoreForError();
                okay = okay && archive::EffectTableRestoreLeaseIsActive();
            }
            workerResults[worker] = okay ? 1 : 0;
        });
    }
    while (ready.load(std::memory_order_acquire) != workerCount)
        std::this_thread::yield();
    go.store(true, std::memory_order_release);
    for (std::thread &worker : workers)
        worker.join();
    for (const int result : workerResults)
        CHECK(result == 1);

    std::vector<std::uint8_t> contenderArchive =
        WriteArchive(MakeLogicalTable({{"fx/contender", 601u}}), false);
    archive::EffectTableRestoreStatus contenderStatus =
        archive::EffectTableRestoreStatus::UnsafeFailure;
    bool contenderCursorUnchanged = false;
    std::thread contender([&]() {
        MemoryFile contenderReader{};
        InitReader(contenderReader, contenderArchive, false);
        const int cursorBefore = contenderReader.bytesUsed;
        int contenderIdentity = 0;
        const std::vector<EntrySpec> contenderEntries{{"fx/contender", 601u}};
        CallbackState contenderState =
            MakeState(&contenderIdentity, 602u, contenderEntries);
        const archive::EffectTableRestoreResult result =
            archive::RestoreEffectTableNoReport(
                &contenderReader,
                &contenderIdentity,
                602u,
                MakeCallbacks(&contenderState));
        contenderStatus = result.status;
        contenderCursorUnchanged = contenderReader.bytesUsed == cursorBefore;
        CloseReader(contenderReader);
    });
    contender.join();
    CHECK(contenderStatus == archive::EffectTableRestoreStatus::Busy);
    CHECK(contenderCursorUnchanged);
    CHECK(archive::EffectTableRestoreLeaseIsActive());
    CHECK(archive::EffectTableRestoreFind(
        held.lease, archive::EffectDefinitionKey32{501u})
        == &state.definitions[0]);
    CHECK(archive::ReleaseEffectTableRestore(held.lease)
        == archive::EffectTableRestoreStatus::Success);
    CHECK(!archive::EffectTableRestoreLeaseIsActive());
}

void TestFailedCapacityClearsStaleWorkspace()
{
    const std::vector<EntrySpec> excessive =
        MakeCapacityTable(archive::EFFECT_TABLE_RESTORE_CAPACITY + 1u);
    RunMalformedRestore(
        MakeLogicalTable(excessive),
        archive::EffectTableRestoreStatus::CapacityExceeded,
        false,
        false);

    const std::vector<EntrySpec> fresh{{"fx/fresh", 7001u}};
    std::vector<std::uint8_t> archiveBytes =
        WriteArchive(MakeLogicalTable(fresh), false);
    MemoryFile reader{};
    InitReader(reader, archiveBytes, false);
    int identityToken = 0;
    CallbackState state = MakeState(&identityToken, 7002u, fresh);
    const archive::EffectTableRestoreResult result =
        archive::RestoreEffectTableNoReport(
            &reader,
            &identityToken,
            7002u,
            MakeCallbacks(&state));
    CHECK(result.status == archive::EffectTableRestoreStatus::Success);
    CHECK(result.entryCount == 1u);
    CHECK(archive::EffectTableRestoreFind(
        result.lease, archive::EffectDefinitionKey32{1u}) == nullptr);
    CHECK(archive::EffectTableRestoreFind(
        result.lease, archive::EffectDefinitionKey32{7001u})
        == &state.definitions[0]);
    CHECK(archive::ReleaseEffectTableRestore(result.lease)
        == archive::EffectTableRestoreStatus::Success);
    CloseReader(reader);
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
    TestValidTables();
    TestMaximumCapacity();
    TestMalformedTables();
    TestInvalidNames();
    TestDuplicatePolicies();
    TestRegistrationFailureAtomicity();
    TestInvalidArgumentsAndState();
    TestLifecycleRejectionAndRelease();
    TestReentryIsBusyBeforeRead();
    TestCallbackAbandonment();
    TestRegistrationUsesStagedNames();
    TestBusyStaleLeaseAndReuse();
    TestForeignOwnerAndContention();
    TestFailedCapacityClearsStaleWorkspace();
    CHECK(unexpectedReports.load(std::memory_order_relaxed) == 0);
    CHECK(!archive::EffectTableRestoreLeaseIsActive());

    const int failureCount = failures.load(std::memory_order_relaxed);
    if (failureCount != 0)
    {
        std::fprintf(
            stderr,
            "%d effect-table restore test(s) failed\n",
            failureCount);
        return 1;
    }
    std::puts("Effect-table transactional restore tests passed");
    return 0;
}
