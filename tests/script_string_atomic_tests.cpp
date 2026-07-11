#include <script/scr_string_atomic.h>

#include <atomic>
#include <barrier>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
namespace string_atomic = scr_string_atomic;

int fail(const char *const what)
{
    std::fprintf(stderr, "script-string atomic test failed: %s\n", what);
    return 1;
}

bool hasWord(
    const volatile std::uint32_t *const word,
    const std::uint16_t refCount,
    const std::uint8_t user,
    const std::uint8_t byteLength)
{
    const std::uint32_t value = string_atomic::Load(word);
    return string_atomic::RefCount(value) == refCount
        && string_atomic::User(value) == user
        && string_atomic::ByteLength(value) == byteLength;
}

bool deterministicContracts()
{
    constexpr std::uint32_t packed =
        string_atomic::Pack(UINT16_C(0x1234), UINT8_C(0x56), UINT8_C(0x78));
    static_assert(packed == UINT32_C(0x78561234));
    static_assert(string_atomic::RefCount(packed) == UINT16_C(0x1234));
    static_assert(string_atomic::User(packed) == UINT8_C(0x56));
    static_assert(string_atomic::ByteLength(packed) == UINT8_C(0x78));

    volatile std::uint32_t word =
        string_atomic::Pack(1, UINT8_C(0x12), UINT8_C(0xa5));
    if (string_atomic::TryRemoveRefUnlessLast(&word)
            != string_atomic::RemoveRefAttempt::LastReference
        || !hasWord(&word, 1, UINT8_C(0x12), UINT8_C(0xa5)))
    {
        return false;
    }
    if (!string_atomic::TryAddRef(&word)
        || !hasWord(&word, 2, UINT8_C(0x12), UINT8_C(0xa5)))
    {
        return false;
    }

    if (string_atomic::TryRemoveRefUnlessLast(&word)
            != string_atomic::RemoveRefAttempt::Removed
        || !hasWord(&word, 1, UINT8_C(0x12), UINT8_C(0xa5)))
    {
        return false;
    }

    const string_atomic::RemoveRefResult userOwnedFinal =
        string_atomic::TryRemoveRef(&word);
    if (userOwnedFinal.success || userOwnedFinal.reachedZero
        || !hasWord(&word, 1, UINT8_C(0x12), UINT8_C(0xa5)))
    {
        return false;
    }

    word = string_atomic::Pack(1, 0, UINT8_C(0xa5));

    const string_atomic::RemoveRefResult finalRemove =
        string_atomic::TryRemoveRef(&word);
    if (!finalRemove.success || !finalRemove.reachedZero
        || !hasWord(&word, 0, 0, UINT8_C(0xa5)))
    {
        return false;
    }

    const string_atomic::RemoveRefResult underflow =
        string_atomic::TryRemoveRef(&word);
    if (underflow.success || underflow.reachedZero
        || string_atomic::TryAddRef(&word)
        || !hasWord(&word, 0, 0, UINT8_C(0xa5)))
    {
        return false;
    }

    word = string_atomic::Pack(2, UINT8_C(0x06), UINT8_C(0xa5));
    const string_atomic::RemoveUserRefResult removedUser =
        string_atomic::RemoveUserRef(&word, UINT8_C(0x02));
    if (removedUser.status != string_atomic::RemoveUserRefStatus::Removed
        || removedUser.reachedZero
        || !hasWord(&word, 1, UINT8_C(0x04), UINT8_C(0xa5)))
    {
        return false;
    }
    const string_atomic::RemoveUserRefResult absentUser =
        string_atomic::RemoveUserRef(&word, UINT8_C(0x02));
    if (absentUser.status != string_atomic::RemoveUserRefStatus::NotPresent
        || absentUser.reachedZero
        || !hasWord(&word, 1, UINT8_C(0x04), UINT8_C(0xa5)))
    {
        return false;
    }
    const string_atomic::RemoveUserRefResult finalUser =
        string_atomic::RemoveUserRef(&word, UINT8_C(0x04));
    if (finalUser.status != string_atomic::RemoveUserRefStatus::Removed
        || !finalUser.reachedZero
        || !hasWord(&word, 0, 0, UINT8_C(0xa5)))
    {
        return false;
    }
    const string_atomic::RemoveUserRefResult invalidUser =
        string_atomic::RemoveUserRef(&word, UINT8_C(0x04));
    if (invalidUser.status != string_atomic::RemoveUserRefStatus::NotPresent
        || invalidUser.reachedZero)
    {
        return false;
    }

    word = string_atomic::Pack(
        string_atomic::kMaxRefCount,
        UINT8_C(0x12),
        UINT8_C(0xa5));
    if (string_atomic::TryAddRef(&word)
        || !hasWord(
            &word,
            string_atomic::kMaxRefCount,
            UINT8_C(0x12),
            UINT8_C(0xa5)))
    {
        return false;
    }

    word = string_atomic::Pack(1, 0, UINT8_C(0x7e));
    if (string_atomic::AddUserRef(&word, UINT8_C(0x04))
            != string_atomic::AddUserRefResult::Added
        || !hasWord(&word, 2, UINT8_C(0x04), UINT8_C(0x7e)))
    {
        return false;
    }
    if (string_atomic::AddUserRef(&word, UINT8_C(0x04))
            != string_atomic::AddUserRefResult::AlreadyPresent
        || !hasWord(&word, 2, UINT8_C(0x04), UINT8_C(0x7e)))
    {
        return false;
    }
    if (string_atomic::AddUserRef(&word, 0)
            != string_atomic::AddUserRefResult::Added
        || !hasWord(&word, 3, UINT8_C(0x04), UINT8_C(0x7e)))
    {
        return false;
    }

    word = string_atomic::Pack(2, 0, UINT8_C(0xe7));
    if (string_atomic::TransferRefToUser(&word, 0)
            != string_atomic::TransferRefToUserResult::Invalid
        || string_atomic::TransferRefToUser(&word, UINT8_C(0x20))
            != string_atomic::TransferRefToUserResult::ClaimedUser
        || !hasWord(&word, 2, UINT8_C(0x20), UINT8_C(0xe7)))
    {
        return false;
    }
    if (string_atomic::TransferRefToUser(&word, UINT8_C(0x20))
            != string_atomic::TransferRefToUserResult::ReleasedDuplicate
        || !hasWord(&word, 1, UINT8_C(0x20), UINT8_C(0xe7)))
    {
        return false;
    }
    if (string_atomic::TransferRefToUser(&word, UINT8_C(0x20))
            != string_atomic::TransferRefToUserResult::Invalid
        || !hasWord(&word, 1, UINT8_C(0x20), UINT8_C(0xe7)))
    {
        return false;
    }

    word = string_atomic::Pack(3, UINT8_C(0x0b), UINT8_C(0x91));
    const string_atomic::RemoveUserRefResult removedMiddleUser =
        string_atomic::RemoveUserRef(&word, UINT8_C(0x02));
    if (removedMiddleUser.status
            != string_atomic::RemoveUserRefStatus::Removed
        || removedMiddleUser.reachedZero
        || !hasWord(&word, 2, UINT8_C(0x09), UINT8_C(0x91)))
    {
        return false;
    }
    if (string_atomic::TransferUser(
            &word,
            UINT8_C(0x01),
            UINT8_C(0x04)) != string_atomic::TransferUserResult::Transferred
        || !hasWord(&word, 2, UINT8_C(0x0c), UINT8_C(0x91)))
    {
        return false;
    }

    word = string_atomic::Pack(0, UINT8_C(0xff), UINT8_C(0x44));
    const string_atomic::RemoveUserRefResult zeroUser =
        string_atomic::RemoveUserRef(&word, UINT8_C(0x01));
    if (zeroUser.status != string_atomic::RemoveUserRefStatus::Invalid
        || zeroUser.reachedZero
        || string_atomic::AddUserRef(&word, UINT8_C(0x01))
            != string_atomic::AddUserRefResult::Invalid
        || string_atomic::TransferRefToUser(&word, UINT8_C(0x01))
            != string_atomic::TransferRefToUserResult::Invalid
        || string_atomic::TransferUser(
            &word,
            UINT8_C(0x01),
            UINT8_C(0x02)) != string_atomic::TransferUserResult::Invalid
        || !hasWord(&word, 0, UINT8_C(0xff), UINT8_C(0x44)))
    {
        return false;
    }

    word = string_atomic::Pack(2, UINT8_C(0x03), UINT8_C(0x44));
    if (string_atomic::TransferUser(
            &word,
            UINT8_C(0x01),
            0) != string_atomic::TransferUserResult::Invalid
        || string_atomic::TransferUser(
            &word,
            UINT8_C(0x01),
            UINT8_C(0x02))
            != string_atomic::TransferUserResult::ReleasedDuplicate
        || !hasWord(&word, 1, UINT8_C(0x02), UINT8_C(0x44)))
    {
        return false;
    }

    word = string_atomic::Pack(
        string_atomic::kMaxRefCount,
        0,
        UINT8_C(0x44));
    return string_atomic::AddUserRef(&word, UINT8_C(0x01))
            == string_atomic::AddUserRefResult::Invalid
        && hasWord(
            &word,
            string_atomic::kMaxRefCount,
            0,
            UINT8_C(0x44));
}

bool sameUserClaimContention()
{
    constexpr int threadCount = 32;
    volatile std::uint32_t word =
        string_atomic::Pack(1, 0, UINT8_C(0xd3));
    std::barrier start(threadCount + 1);
    std::atomic<int> added{0};
    std::atomic<int> alreadyPresent{0};
    std::atomic<int> invalid{0};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&] {
            start.arrive_and_wait();
            switch (string_atomic::AddUserRef(&word, UINT8_C(0x08)))
            {
            case string_atomic::AddUserRefResult::Added:
                ++added;
                break;
            case string_atomic::AddUserRefResult::AlreadyPresent:
                ++alreadyPresent;
                break;
            case string_atomic::AddUserRefResult::Invalid:
                ++invalid;
                break;
            }
        });
    }

    start.arrive_and_wait();
    for (std::thread &thread : threads)
        thread.join();

    return added.load() == 1
        && alreadyPresent.load() == threadCount - 1
        && invalid.load() == 0
        && hasWord(&word, 2, UINT8_C(0x08), UINT8_C(0xd3));
}

bool transferRefContention()
{
    constexpr int iterationCount = 128;
    for (int iteration = 0; iteration < iterationCount; ++iteration)
    {
        volatile std::uint32_t word =
            string_atomic::Pack(2, 0, UINT8_C(0x6d));
        std::barrier start(3);
        std::atomic<int> claimed{0};
        std::atomic<int> released{0};
        std::atomic<int> invalid{0};

        const auto transfer = [&] {
            start.arrive_and_wait();
            switch (string_atomic::TransferRefToUser(
                &word,
                UINT8_C(0x40)))
            {
            case string_atomic::TransferRefToUserResult::ClaimedUser:
                ++claimed;
                break;
            case string_atomic::TransferRefToUserResult::ReleasedDuplicate:
                ++released;
                break;
            case string_atomic::TransferRefToUserResult::Invalid:
                ++invalid;
                break;
            }
        };

        std::thread first(transfer);
        std::thread second(transfer);
        start.arrive_and_wait();
        first.join();
        second.join();

        if (claimed.load() != 1 || released.load() != 1
            || invalid.load() != 0
            || !hasWord(&word, 1, UINT8_C(0x40), UINT8_C(0x6d)))
        {
            return false;
        }
    }
    return true;
}

bool twoWayUserTransferContention()
{
    constexpr int iterationCount = 128;
    for (int iteration = 0; iteration < iterationCount; ++iteration)
    {
        volatile std::uint32_t word = string_atomic::Pack(
            UINT16_C(0x1234),
            UINT8_C(0x03),
            UINT8_C(0xfe));
        std::barrier start(3);
        string_atomic::TransferUserResult firstResult =
            string_atomic::TransferUserResult::Invalid;
        string_atomic::TransferUserResult secondResult =
            string_atomic::TransferUserResult::Invalid;

        std::thread first([&] {
            start.arrive_and_wait();
            firstResult = string_atomic::TransferUser(
                &word,
                UINT8_C(0x01),
                UINT8_C(0x04));
        });
        std::thread second([&] {
            start.arrive_and_wait();
            secondResult = string_atomic::TransferUser(
                &word,
                UINT8_C(0x02),
                UINT8_C(0x08));
        });

        start.arrive_and_wait();
        first.join();
        second.join();

        if (firstResult != string_atomic::TransferUserResult::Transferred
            || secondResult != string_atomic::TransferUserResult::Transferred
            || !hasWord(
                &word,
                UINT16_C(0x1234),
                UINT8_C(0x0c),
                UINT8_C(0xfe)))
        {
            return false;
        }
    }
    return true;
}

bool exactZeroTransitionContention()
{
    constexpr int initialRefCount = 32;
    constexpr int threadCount = 64;
    volatile std::uint32_t word = string_atomic::Pack(
        initialRefCount,
        0,
        UINT8_C(0xbc));
    std::barrier start(threadCount + 1);
    std::atomic<int> removed{0};
    std::atomic<int> reachedZero{0};
    std::atomic<int> invalid{0};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&] {
            start.arrive_and_wait();
            const string_atomic::RemoveRefResult result =
                string_atomic::TryRemoveRef(&word);
            if (!result.success)
            {
                ++invalid;
                return;
            }
            ++removed;
            if (result.reachedZero)
                ++reachedZero;
        });
    }

    start.arrive_and_wait();
    for (std::thread &thread : threads)
        thread.join();

    return removed.load() == initialRefCount
        && reachedZero.load() == 1
        && invalid.load() == threadCount - initialRefCount
        && hasWord(&word, 0, 0, UINT8_C(0xbc));
}

bool userRemovalContention()
{
    constexpr int iterationCount = 128;
    for (int iteration = 0; iteration < iterationCount; ++iteration)
    {
        volatile std::uint32_t word = string_atomic::Pack(
            2,
            UINT8_C(0x03),
            UINT8_C(0x9a));
        std::barrier start(3);
        string_atomic::RemoveUserRefResult first{
            string_atomic::RemoveUserRefStatus::Invalid,
            false};
        string_atomic::RemoveUserRefResult second{
            string_atomic::RemoveUserRefStatus::Invalid,
            false};

        std::thread firstThread([&] {
            start.arrive_and_wait();
            first = string_atomic::RemoveUserRef(&word, UINT8_C(0x01));
        });
        std::thread secondThread([&] {
            start.arrive_and_wait();
            second = string_atomic::RemoveUserRef(&word, UINT8_C(0x02));
        });

        start.arrive_and_wait();
        firstThread.join();
        secondThread.join();

        if (first.status != string_atomic::RemoveUserRefStatus::Removed
            || second.status != string_atomic::RemoveUserRefStatus::Removed
            || static_cast<int>(first.reachedZero)
                    + static_cast<int>(second.reachedZero) != 1
            || !hasWord(&word, 0, 0, UINT8_C(0x9a)))
        {
            return false;
        }
    }

    constexpr int threadCount = 32;
    volatile std::uint32_t word = string_atomic::Pack(
        1,
        UINT8_C(0x04),
        UINT8_C(0x7b));
    std::barrier start(threadCount + 1);
    std::atomic<int> removed{0};
    std::atomic<int> notPresent{0};
    std::atomic<int> invalid{0};
    std::atomic<int> reachedZero{0};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&] {
            start.arrive_and_wait();
            const string_atomic::RemoveUserRefResult result =
                string_atomic::RemoveUserRef(&word, UINT8_C(0x04));
            switch (result.status)
            {
            case string_atomic::RemoveUserRefStatus::Removed:
                ++removed;
                break;
            case string_atomic::RemoveUserRefStatus::NotPresent:
                ++notPresent;
                break;
            case string_atomic::RemoveUserRefStatus::Invalid:
                ++invalid;
                break;
            }
            if (result.reachedZero)
                ++reachedZero;
        });
    }

    start.arrive_and_wait();
    for (std::thread &thread : threads)
        thread.join();

    return removed.load() == 1
        && notPresent.load() == threadCount - 1
        && invalid.load() == 0
        && reachedZero.load() == 1
        && hasWord(&word, 0, 0, UINT8_C(0x7b));
}
} // namespace

int main()
{
    if (!deterministicContracts())
        return fail("deterministic packed-word contracts");
    if (!sameUserClaimContention())
        return fail("same-user contention must add exactly one reference");
    if (!transferRefContention())
        return fail("two claimants must claim once and release one duplicate");
    if (!twoWayUserTransferContention())
        return fail("two-way user transfer must preserve both updates");
    if (!exactZeroTransitionContention())
        return fail("exactly one remover must own the zero transition");
    if (!userRemovalContention())
        return fail("user-reference removals must own one zero transition");
    return 0;
}
