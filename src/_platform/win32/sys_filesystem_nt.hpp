#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>

// Private NT ABI declarations for handle-relative filesystem operations.
// Keeping them together makes the kernel record layout independently auditable.
namespace
{
using KisakNtStatus = std::int32_t;
// cppcheck-suppress misra-c2012-12.3 -- A C++ template argument separator, not the comma operator.
using KisakFileId = std::array<unsigned char, 16>;

// These fields are consumed by sys_filesystem.cpp and the NT kernel ABI.
// Standalone header analysis cannot see those consumers; none may be removed.
// cppcheck-suppress-begin unusedStructMember
struct KisakUnicodeString
{
    std::uint16_t Length;
    std::uint16_t MaximumLength;
    wchar_t *Buffer;
};

struct KisakIoStatusBlock
{
    union
    {
        KisakNtStatus Status;
        void *Pointer;
    };
    std::uintptr_t Information;
};

struct KisakObjectAttributes
{
    std::uint32_t Length;
    void *RootDirectory;
    KisakUnicodeString *ObjectName;
    std::uint32_t Attributes;
    void *SecurityDescriptor;
    void *SecurityQualityOfService;
};

struct KisakFileIdExtdDirectoryInformation
{
    std::uint32_t NextEntryOffset;
    std::uint32_t FileIndex;
    std::int64_t CreationTime;
    std::int64_t LastAccessTime;
    std::int64_t LastWriteTime;
    std::int64_t ChangeTime;
    std::int64_t EndOfFile;
    std::int64_t AllocationSize;
    std::uint32_t FileAttributes;
    std::uint32_t FileNameLength;
    std::uint32_t EaSize;
    std::uint32_t ReparsePointTag;
    // cppcheck-suppress misra-c2012-12.3 -- C++ array type, not a comma expression.
    KisakFileId FileId;
    wchar_t FileName[1];
};

using KisakNtCreateFileFn = KisakNtStatus (__stdcall *)(
    HANDLE *fileHandle,
    std::uint32_t desiredAccess,
    KisakObjectAttributes *objectAttributes,
    KisakIoStatusBlock *ioStatusBlock,
    std::int64_t *allocationSize,
    std::uint32_t fileAttributes,
    std::uint32_t shareAccess,
    std::uint32_t createDisposition,
    std::uint32_t createOptions,
    void *eaBuffer,
    std::uint32_t eaLength);

using KisakNtQueryDirectoryFileFn = KisakNtStatus (__stdcall *)(
    HANDLE fileHandle,
    HANDLE event,
    void *apcRoutine,
    void *apcContext,
    KisakIoStatusBlock *ioStatusBlock,
    void *fileInformation,
    std::uint32_t length,
    std::uint32_t fileInformationClass,
    std::uint32_t returnSingleEntry,
    KisakUnicodeString *fileName,
    std::uint32_t restartScan);

struct KisakNtProcedures
{
    KisakNtCreateFileFn createFile;
    KisakNtQueryDirectoryFileFn queryDirectoryFile;
};

// cppcheck-suppress-end unusedStructMember
} // namespace
