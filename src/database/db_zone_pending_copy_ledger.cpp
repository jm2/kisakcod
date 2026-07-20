#include <database/db_zone_pending_copy_ledger.h>

#include <database/db_zone_slots.h>

#include <limits>

namespace db::zone_pending_copy
{
namespace
{
constexpr std::uint8_t kReceiptPhaseWitnessMask = UINT8_C(0xA7);
constexpr std::uint8_t kLedgerPhaseWitnessMask = UINT8_C(0x5D);

[[nodiscard]] constexpr bool IsNullKey(
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return key == zone_load::ZoneLoadContextKey{};
}

[[nodiscard]] constexpr bool IsUsableKey(
    const zone_load::ZoneLoadContextKey &key) noexcept
{
    return static_cast<bool>(key)
        && zone_slots::IsUsableZoneSlot(key.slot);
}

[[nodiscard]] constexpr bool IsAssetEntryIndex(
    const std::uint32_t index) noexcept
{
    return index >= kFirstAssetEntryIndex
        && index <= kLastAssetEntryIndex;
}

[[nodiscard]] constexpr bool IsReceiptPhase(
    const PendingCopyAdmissionPhase phase) noexcept
{
    switch (phase)
    {
    case PendingCopyAdmissionPhase::Pristine:
    case PendingCopyAdmissionPhase::Collecting:
    case PendingCopyAdmissionPhase::Prepared:
    case PendingCopyAdmissionPhase::Admitting:
    case PendingCopyAdmissionPhase::Admitted:
    case PendingCopyAdmissionPhase::Drained:
    case PendingCopyAdmissionPhase::Discarded:
    case PendingCopyAdmissionPhase::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsTerminalReceiptPhase(
    const PendingCopyAdmissionPhase phase) noexcept
{
    return phase == PendingCopyAdmissionPhase::Drained
        || phase == PendingCopyAdmissionPhase::Discarded;
}

[[nodiscard]] constexpr PendingCopyStatus ValidateRequestedKey(
    const zone_load::ZoneLoadContextKey &bound,
    const zone_load::ZoneLoadContextKey &requested) noexcept
{
    if (!IsUsableKey(requested))
        return PendingCopyStatus::InvalidKey;
    if (bound == requested)
        return PendingCopyStatus::Success;
    return IsUsableKey(bound) && bound.slot == requested.slot
        ? PendingCopyStatus::StaleKey
        : PendingCopyStatus::InvalidKey;
}

[[nodiscard]] bool ObjectSpan(
    const void *const object,
    const std::size_t size,
    std::uintptr_t *const begin,
    std::uintptr_t *const end) noexcept
{
    if (!object || !size || !begin || !end)
        return false;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(object);
    if (size > (std::numeric_limits<std::uintptr_t>::max)() - address)
        return false;
    *begin = address;
    *end = address + size;
    return true;
}

[[nodiscard]] constexpr bool RangesOverlap(
    const std::uintptr_t leftBegin,
    const std::uintptr_t leftEnd,
    const std::uintptr_t rightBegin,
    const std::uintptr_t rightEnd) noexcept
{
    return leftBegin < rightEnd && rightBegin < leftEnd;
}

[[nodiscard]] bool ObjectsDisjoint(
    const void *const first,
    const std::size_t firstSize,
    const void *const second,
    const std::size_t secondSize) noexcept
{
    std::uintptr_t firstBegin = 0;
    std::uintptr_t firstEnd = 0;
    std::uintptr_t secondBegin = 0;
    std::uintptr_t secondEnd = 0;
    return ObjectSpan(first, firstSize, &firstBegin, &firstEnd)
        && ObjectSpan(second, secondSize, &secondBegin, &secondEnd)
        && !RangesOverlap(
            firstBegin, firstEnd, secondBegin, secondEnd);
}

[[nodiscard]] constexpr bool IsDefaultRecord(
    const PendingCopyRecord &record) noexcept
{
    return record == PendingCopyRecord{};
}

[[nodiscard]] constexpr bool IsCanonicalRecord(
    const PendingCopyRecord &record) noexcept
{
    return IsUsableKey(record.key)
        && IsAssetEntryIndex(record.assetEntryIndex)
        && record.reserved == 0;
}
} // namespace

PendingCopyAdmissionReceipt::PendingCopyAdmissionReceipt() noexcept
{
    setPhase(PendingCopyAdmissionPhase::Pristine);
}

PendingCopyAdmissionReceipt::~PendingCopyAdmissionReceipt() noexcept = default;

PendingCopyAdmissionPhase PendingCopyAdmissionReceipt::phase() const noexcept
{
    return phase_;
}

const zone_load::ZoneLoadContextKey &
PendingCopyAdmissionReceipt::key() const noexcept
{
    return key_;
}

const zone_load::ZoneLoadContextSlot *
PendingCopyAdmissionReceipt::lifecycle() const noexcept
{
    return lifecycle_;
}

std::uint32_t PendingCopyAdmissionReceipt::recordCount() const noexcept
{
    if (!isCanonical() || IsTerminalReceiptPhase(phase_)
        || phase_ == PendingCopyAdmissionPhase::Pristine
        || phase_ == PendingCopyAdmissionPhase::UnsafeFailure
        || !ledger_ || !ledger_->isCanonical()
        || generationIndex_ >= ledger_->generationCount_)
    {
        return 0;
    }
    const PendingCopyLedger::GenerationDescriptor &descriptor =
        ledger_->generations_[generationIndex_];
    return descriptor.receipt == this
            && descriptor.key == key_
            && descriptor.serial == generationSerial_
        ? descriptor.recordCount
        : 0;
}

bool PendingCopyAdmissionReceipt::canonical() const noexcept
{
    return isCanonical();
}

bool PendingCopyAdmissionReceipt::isPristine() const noexcept
{
    return IsNullKey(key_) && ledger_ == nullptr && lifecycle_ == nullptr
        && self_ == nullptr && completionContext_ == nullptr
        && completion_ == nullptr && generationSerial_ == 0
        && generationIndex_ == kInvalidGenerationIndex
        && phase_ == PendingCopyAdmissionPhase::Pristine
        && phaseWitness_
            == (static_cast<std::uint8_t>(phase_)
                ^ kReceiptPhaseWitnessMask)
        && reserved_[0] == 0 && reserved_[1] == 0;
}

bool PendingCopyAdmissionReceipt::isCanonical() const noexcept
{
    if (!IsReceiptPhase(phase_)
        || phaseWitness_
            != (static_cast<std::uint8_t>(phase_)
                ^ kReceiptPhaseWitnessMask)
        || reserved_[0] != 0 || reserved_[1] != 0)
    {
        return false;
    }
    if (phase_ == PendingCopyAdmissionPhase::Pristine)
        return isPristine();
    if (self_ != this || !ledger_ || !lifecycle_
        || !IsUsableKey(key_) || generationSerial_ == 0)
    {
        return false;
    }

    switch (phase_)
    {
    case PendingCopyAdmissionPhase::Collecting:
        return generationIndex_ != kInvalidGenerationIndex
            && completionContext_ == nullptr && completion_ == nullptr;
    case PendingCopyAdmissionPhase::Prepared:
    case PendingCopyAdmissionPhase::Admitting:
        return generationIndex_ != kInvalidGenerationIndex
            && completion_ != nullptr;
    case PendingCopyAdmissionPhase::Admitted:
        return generationIndex_ != kInvalidGenerationIndex
            && completionContext_ == nullptr && completion_ == nullptr;
    case PendingCopyAdmissionPhase::Drained:
    case PendingCopyAdmissionPhase::Discarded:
        return generationIndex_ == kInvalidGenerationIndex
            && completionContext_ == nullptr && completion_ == nullptr;
    case PendingCopyAdmissionPhase::UnsafeFailure:
        return completion_ == nullptr;
    default:
        return false;
    }
}

void PendingCopyAdmissionReceipt::setPhase(
    const PendingCopyAdmissionPhase phase) noexcept
{
    phase_ = phase;
    phaseWitness_ = static_cast<std::uint8_t>(phase)
        ^ kReceiptPhaseWitnessMask;
}

void PendingCopyAdmissionReceipt::reset() noexcept
{
    key_ = {};
    ledger_ = nullptr;
    lifecycle_ = nullptr;
    self_ = nullptr;
    completionContext_ = nullptr;
    completion_ = nullptr;
    generationSerial_ = 0;
    generationIndex_ = kInvalidGenerationIndex;
    reserved_[0] = 0;
    reserved_[1] = 0;
    setPhase(PendingCopyAdmissionPhase::Pristine);
}

PendingCopyLedger::PendingCopyLedger() noexcept
{
    setPhase(Phase::Pristine);
}

PendingCopyLedger::~PendingCopyLedger() noexcept = default;

bool PendingCopyLedger::initialized() const noexcept
{
    return self_ == this && phase_ != Phase::Pristine;
}

std::uint32_t PendingCopyLedger::recordCount() const noexcept
{
    return hasCanonicalHeader() ? recordCount_ : 0;
}

std::uint32_t PendingCopyLedger::generationCount() const noexcept
{
    return hasCanonicalHeader() ? generationCount_ : 0;
}

bool PendingCopyLedger::canonical() const noexcept
{
    return isCanonical();
}

bool PendingCopyLedger::isPristine() const noexcept
{
    if (self_ != nullptr || nextGenerationSerial_ != 0
        || recordCount_ != 0 || generationCount_ != 0 || drainCursor_ != 0
        || phase_ != Phase::Pristine
        || phaseWitness_
            != (static_cast<std::uint8_t>(phase_)
                ^ kLedgerPhaseWitnessMask)
        || callbackActive_ != 0 || reserved_ != 0)
    {
        return false;
    }
    for (const PendingCopyRecord &record : records_)
    {
        if (!IsDefaultRecord(record))
            return false;
    }
    for (const GenerationDescriptor &descriptor : generations_)
    {
        if (!IsNullKey(descriptor.key) || descriptor.receipt != nullptr
            || descriptor.serial != 0 || descriptor.firstRecord != 0
            || descriptor.recordCount != 0
            || descriptor.phase != GenerationPhase::Empty
            || descriptor.reserved[0] != 0
            || descriptor.reserved[1] != 0
            || descriptor.reserved[2] != 0)
        {
            return false;
        }
    }
    return true;
}

bool AuthenticatePassivePendingCopyLedger(
    const PendingCopyLedger &ledger) noexcept
{
    return ledger.isPristine();
}

bool PendingCopyLedger::hasCanonicalHeader() const noexcept
{
    if (self_ != this || nextGenerationSerial_ == 0
        || recordCount_ > records_.size()
        || generationCount_ > generations_.size()
        || drainCursor_ > recordCount_
        || phaseWitness_
            != (static_cast<std::uint8_t>(phase_)
                ^ kLedgerPhaseWitnessMask)
        || reserved_ != 0 || callbackActive_ > 1)
    {
        return false;
    }
    switch (phase_)
    {
    case Phase::Ready:
    case Phase::AdmissionPrepared:
    case Phase::Draining:
    case Phase::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

bool PendingCopyLedger::descriptorMatchesReceipt(
    const std::uint32_t index) const noexcept
{
    if (index >= generationCount_)
        return false;
    const GenerationDescriptor &descriptor = generations_[index];
    const PendingCopyAdmissionReceipt *const receipt = descriptor.receipt;
    if (!receipt || !receipt->isCanonical()
        || receipt->ledger_ != this || receipt->lifecycle_ == nullptr
        || receipt->key_ != descriptor.key
        || receipt->generationSerial_ != descriptor.serial
        || receipt->generationIndex_ != index)
    {
        return false;
    }
    switch (descriptor.phase)
    {
    case GenerationPhase::Collecting:
        return receipt->phase_ == PendingCopyAdmissionPhase::Collecting;
    case GenerationPhase::Prepared:
        return receipt->phase_ == PendingCopyAdmissionPhase::Prepared;
    case GenerationPhase::Admitted:
        return receipt->phase_ == PendingCopyAdmissionPhase::Admitted
            || (callbackActive_ != 0
                && receipt->phase_
                    == PendingCopyAdmissionPhase::Admitting);
    default:
        return false;
    }
}

bool PendingCopyLedger::isCanonical() const noexcept
{
    if (!hasCanonicalHeader() || phase_ == Phase::UnsafeFailure)
        return false;
    if (phase_ != Phase::Draining && drainCursor_ != 0)
        return false;

    std::uint32_t expectedFirst = 0;
    std::uint32_t preparedCount = 0;
    std::uint32_t admittingCount = 0;
    std::uint64_t previousSerial = 0;
    for (std::uint32_t index = 0; index < generationCount_; ++index)
    {
        const GenerationDescriptor &descriptor = generations_[index];
        if (!IsUsableKey(descriptor.key) || descriptor.serial == 0
            || descriptor.serial <= previousSerial
            || descriptor.serial >= nextGenerationSerial_
            || descriptor.firstRecord != expectedFirst
            || descriptor.recordCount > recordCount_ - expectedFirst
            || descriptor.reserved[0] != 0
            || descriptor.reserved[1] != 0
            || descriptor.reserved[2] != 0
            || !descriptorMatchesReceipt(index))
        {
            return false;
        }
        previousSerial = descriptor.serial;
        for (std::uint32_t prior = 0; prior < index; ++prior)
        {
            if (generations_[prior].key == descriptor.key)
                return false;
        }
        if (descriptor.phase == GenerationPhase::Prepared)
            ++preparedCount;
        if (descriptor.receipt->phase_
            == PendingCopyAdmissionPhase::Admitting)
        {
            if (index + 1 != generationCount_)
                return false;
            ++admittingCount;
        }
        if (index + 1 < generationCount_
            && descriptor.phase != GenerationPhase::Admitted)
        {
            return false;
        }
        if (phase_ == Phase::Draining
            && descriptor.phase != GenerationPhase::Admitted)
        {
            return false;
        }
        for (std::uint32_t offset = 0;
            offset < descriptor.recordCount;
            ++offset)
        {
            const PendingCopyRecord &record =
                records_[expectedFirst + offset];
            if (!IsCanonicalRecord(record)
                || record.key != descriptor.key)
            {
                return false;
            }
        }
        expectedFirst += descriptor.recordCount;
    }
    if (expectedFirst != recordCount_)
        return false;
    if ((phase_ == Phase::AdmissionPrepared && preparedCount != 1)
        || (phase_ != Phase::AdmissionPrepared && preparedCount != 0))
    {
        return false;
    }
    if (callbackActive_ != 0)
    {
        if ((phase_ == Phase::Ready && admittingCount != 1)
            || (phase_ == Phase::Draining && admittingCount != 0)
            || (phase_ != Phase::Ready && phase_ != Phase::Draining))
        {
            return false;
        }
    }
    else if (admittingCount != 0)
    {
        return false;
    }
    for (std::uint32_t index = recordCount_; index < records_.size(); ++index)
    {
        if (!IsDefaultRecord(records_[index]))
            return false;
    }
    for (std::uint32_t index = generationCount_;
        index < generations_.size();
        ++index)
    {
        const GenerationDescriptor &descriptor = generations_[index];
        if (!IsNullKey(descriptor.key) || descriptor.receipt != nullptr
            || descriptor.serial != 0 || descriptor.firstRecord != 0
            || descriptor.recordCount != 0
            || descriptor.phase != GenerationPhase::Empty
            || descriptor.reserved[0] != 0
            || descriptor.reserved[1] != 0
            || descriptor.reserved[2] != 0)
        {
            return false;
        }
    }
    return true;
}

void PendingCopyLedger::setPhase(const Phase phase) noexcept
{
    phase_ = phase;
    phaseWitness_ = static_cast<std::uint8_t>(phase)
        ^ kLedgerPhaseWitnessMask;
}

void PendingCopyLedger::poison() noexcept
{
    callbackActive_ = 0;
    setPhase(Phase::UnsafeFailure);
}

PendingCopyStatus TryInitializePendingCopyLedger(
    PendingCopyLedger *const ledger) noexcept
{
    if (!ledger)
        return PendingCopyStatus::InvalidArgument;
    if (ledger->isPristine())
    {
        ledger->self_ = ledger;
        ledger->nextGenerationSerial_ = 1;
        ledger->setPhase(PendingCopyLedger::Phase::Ready);
        return PendingCopyStatus::Success;
    }
    if (!ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    return ledger->phase_ == PendingCopyLedger::Phase::Ready
            && ledger->recordCount_ == 0
            && ledger->generationCount_ == 0
        ? PendingCopyStatus::Success
        : PendingCopyStatus::InvalidState;
}

PendingCopyStatus TryBeginPendingCopyAdmission(
    PendingCopyLedger *const ledger,
    PendingCopyAdmissionReceipt *const receipt,
    zone_load::ZoneLoadContextSlot *const lifecycle,
    const zone_load::ZoneLoadContextKey &keyArgument) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!ledger || !receipt || !lifecycle)
        return PendingCopyStatus::InvalidArgument;
    if (!IsUsableKey(key))
        return PendingCopyStatus::InvalidKey;

    // This address-only shape check must precede object authentication. In
    // particular, a forged pristine receipt inside ledger/lifecycle storage
    // must be rejected without reading through that forged object pointer.
    if (!ObjectsDisjoint(ledger, sizeof(*ledger), receipt, sizeof(*receipt))
        || !ObjectsDisjoint(
            ledger, sizeof(*ledger), lifecycle, sizeof(*lifecycle))
        || !ObjectsDisjoint(
            receipt, sizeof(*receipt), lifecycle, sizeof(*lifecycle)))
    {
        return PendingCopyStatus::InvalidArgument;
    }
    if (!receipt->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (receipt->phase_ != PendingCopyAdmissionPhase::Pristine)
    {
        const PendingCopyStatus keyStatus =
            ValidateRequestedKey(receipt->key_, key);
        if (keyStatus != PendingCopyStatus::Success)
            return keyStatus;
        if (IsTerminalReceiptPhase(receipt->phase_))
            return PendingCopyStatus::AlreadyComplete;
        if (receipt->ledger_ != ledger || receipt->lifecycle_ != lifecycle)
            return PendingCopyStatus::StaleKey;
        if (receipt->phase_ == PendingCopyAdmissionPhase::Admitting)
            return PendingCopyStatus::Busy;
        if (receipt->phase_ != PendingCopyAdmissionPhase::Collecting
            && receipt->phase_ != PendingCopyAdmissionPhase::Prepared
            && receipt->phase_ != PendingCopyAdmissionPhase::Admitted)
        {
            return PendingCopyStatus::UnsafeFailure;
        }
        if (!ledger->isCanonical())
            return PendingCopyStatus::UnsafeFailure;
        return ledger->callbackActive_ != 0
            ? PendingCopyStatus::Busy
            : PendingCopyStatus::Success;
    }

    if (!ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    if (ledger->phase_ == PendingCopyLedger::Phase::AdmissionPrepared
        || ledger->phase_ == PendingCopyLedger::Phase::Draining)
    {
        return PendingCopyStatus::Busy;
    }
    if (ledger->phase_ != PendingCopyLedger::Phase::Ready)
        return PendingCopyStatus::UnsafeFailure;
    if (!zone_load::ZoneLoadContextKeyMatches(lifecycle, key)
        || lifecycle->phase() != zone_load::ZoneLoadContextPhase::Loading)
    {
        return PendingCopyStatus::InvalidPhase;
    }
    if (ledger->generationCount_ >= ledger->generations_.size())
        return PendingCopyStatus::GenerationCapacityExceeded;
    if (ledger->nextGenerationSerial_ == UINT64_MAX)
        return PendingCopyStatus::GenerationExhausted;
    for (std::uint32_t index = 0;
        index < ledger->generationCount_;
        ++index)
    {
        if (ledger->generations_[index].key == key)
            return PendingCopyStatus::InvalidState;
    }
    if (ledger->generationCount_ != 0
        && ledger->generations_[ledger->generationCount_ - 1].phase
            != PendingCopyLedger::GenerationPhase::Admitted)
    {
        return PendingCopyStatus::Busy;
    }

    const std::uint32_t generationIndex = ledger->generationCount_;
    const std::uint64_t serial = ledger->nextGenerationSerial_;
    receipt->key_ = key;
    receipt->ledger_ = ledger;
    receipt->lifecycle_ = lifecycle;
    receipt->completionContext_ = nullptr;
    receipt->completion_ = nullptr;
    receipt->generationSerial_ = serial;
    receipt->generationIndex_ = generationIndex;
    receipt->reserved_[0] = 0;
    receipt->reserved_[1] = 0;
    receipt->setPhase(PendingCopyAdmissionPhase::Collecting);
    receipt->self_ = receipt;

    PendingCopyLedger::GenerationDescriptor &descriptor =
        ledger->generations_[generationIndex];
    descriptor.key = key;
    descriptor.receipt = receipt;
    descriptor.serial = serial;
    descriptor.firstRecord = ledger->recordCount_;
    descriptor.recordCount = 0;
    descriptor.phase = PendingCopyLedger::GenerationPhase::Collecting;
    descriptor.reserved[0] = 0;
    descriptor.reserved[1] = 0;
    descriptor.reserved[2] = 0;
    ++ledger->generationCount_;
    ++ledger->nextGenerationSerial_;
    return PendingCopyStatus::Success;
}

PendingCopyStatus TryAppendPendingCopyRecord(
    PendingCopyAdmissionReceipt *const receipt,
    const zone_load::ZoneLoadContextKey &keyArgument,
    const std::uint32_t assetEntryIndex) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!receipt)
        return PendingCopyStatus::InvalidArgument;
    if (!receipt->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    const PendingCopyStatus keyStatus =
        ValidateRequestedKey(receipt->key_, key);
    if (keyStatus != PendingCopyStatus::Success)
        return keyStatus;
    if (!IsAssetEntryIndex(assetEntryIndex))
        return PendingCopyStatus::InvalidRecord;
    if (receipt->phase_ == PendingCopyAdmissionPhase::Admitting)
        return PendingCopyStatus::Busy;
    if (receipt->phase_ != PendingCopyAdmissionPhase::Collecting)
        return PendingCopyStatus::InvalidPhase;

    PendingCopyLedger *const ledger = receipt->ledger_;
    if (!ledger || !ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    if (ledger->phase_ != PendingCopyLedger::Phase::Ready
        || receipt->generationIndex_ >= ledger->generationCount_)
    {
        return PendingCopyStatus::InvalidState;
    }
    if (!zone_load::ZoneLoadContextKeyMatches(receipt->lifecycle_, key)
        || receipt->lifecycle_->phase()
            != zone_load::ZoneLoadContextPhase::Loading)
    {
        return PendingCopyStatus::InvalidPhase;
    }
    PendingCopyLedger::GenerationDescriptor &descriptor =
        ledger->generations_[receipt->generationIndex_];
    if (receipt->generationIndex_ + 1 != ledger->generationCount_
        || descriptor.receipt != receipt || descriptor.key != key
        || descriptor.serial != receipt->generationSerial_
        || descriptor.phase
            != PendingCopyLedger::GenerationPhase::Collecting
        || descriptor.firstRecord + descriptor.recordCount
            != ledger->recordCount_)
    {
        return PendingCopyStatus::UnsafeFailure;
    }
    if (ledger->recordCount_ >= ledger->records_.size())
        return PendingCopyStatus::CapacityExceeded;
    if (!IsDefaultRecord(ledger->records_[ledger->recordCount_]))
        return PendingCopyStatus::UnsafeFailure;

    ledger->records_[ledger->recordCount_] = {key, assetEntryIndex, 0};
    ++ledger->recordCount_;
    ++descriptor.recordCount;
    return PendingCopyStatus::Success;
}

PendingCopyStatus TryReadPendingCopyRecord(
    const PendingCopyAdmissionReceipt *const receipt,
    const zone_load::ZoneLoadContextKey &keyArgument,
    const std::uint32_t ordinal,
    PendingCopyRecord *const outRecord) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!receipt || !outRecord)
        return PendingCopyStatus::InvalidArgument;
    if (!receipt->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    const PendingCopyStatus keyStatus =
        ValidateRequestedKey(receipt->key_, key);
    if (keyStatus != PendingCopyStatus::Success)
        return keyStatus;
    if (receipt->phase_ == PendingCopyAdmissionPhase::Admitting)
        return PendingCopyStatus::Busy;
    if (receipt->phase_ != PendingCopyAdmissionPhase::Collecting
        && receipt->phase_ != PendingCopyAdmissionPhase::Prepared
        && receipt->phase_ != PendingCopyAdmissionPhase::Admitted)
    {
        return PendingCopyStatus::InvalidPhase;
    }

    const PendingCopyLedger *const ledger = receipt->ledger_;
    if (!ledger)
        return PendingCopyStatus::UnsafeFailure;
    if (!ObjectsDisjoint(
            ledger, sizeof(*ledger), outRecord, sizeof(*outRecord))
        || !ObjectsDisjoint(
            receipt, sizeof(*receipt), outRecord, sizeof(*outRecord))
        || !ObjectsDisjoint(
            receipt->lifecycle_,
            sizeof(*receipt->lifecycle_),
            outRecord,
            sizeof(*outRecord)))
    {
        return PendingCopyStatus::InvalidArgument;
    }
    if (!ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    if (receipt->generationIndex_ >= ledger->generationCount_)
    {
        return PendingCopyStatus::UnsafeFailure;
    }
    const PendingCopyLedger::GenerationDescriptor &descriptor =
        ledger->generations_[receipt->generationIndex_];
    if (descriptor.receipt != receipt || descriptor.key != key
        || descriptor.serial != receipt->generationSerial_
        || descriptor.firstRecord > ledger->recordCount_
        || descriptor.recordCount
            > ledger->recordCount_ - descriptor.firstRecord)
    {
        return PendingCopyStatus::UnsafeFailure;
    }
    if (ordinal >= descriptor.recordCount)
        return PendingCopyStatus::InvalidRecord;
    const PendingCopyRecord candidate =
        ledger->records_[descriptor.firstRecord + ordinal];
    if (!IsCanonicalRecord(candidate) || candidate.key != key)
        return PendingCopyStatus::UnsafeFailure;
    *outRecord = candidate;
    return PendingCopyStatus::Success;
}

PendingCopyStatus TryPreparePendingCopyAdmission(
    PendingCopyAdmissionReceipt *const receipt,
    const zone_load::ZoneLoadContextKey &keyArgument,
    const PendingCopyAdmissionCompletion &completion) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!receipt || !completion.complete)
        return PendingCopyStatus::InvalidArgument;
    if (!receipt->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    const PendingCopyStatus keyStatus =
        ValidateRequestedKey(receipt->key_, key);
    if (keyStatus != PendingCopyStatus::Success)
        return keyStatus;
    if (receipt->phase_ == PendingCopyAdmissionPhase::Prepared)
    {
        PendingCopyLedger *const ledger = receipt->ledger_;
        if (!ledger || !ledger->isCanonical())
            return PendingCopyStatus::UnsafeFailure;
        if (ledger->callbackActive_ != 0)
            return PendingCopyStatus::Busy;
        return receipt->completionContext_ == completion.context
                && receipt->completion_ == completion.complete
            ? PendingCopyStatus::Success
            : PendingCopyStatus::InvalidState;
    }
    if (receipt->phase_ == PendingCopyAdmissionPhase::Admitting)
        return PendingCopyStatus::Busy;
    if (receipt->phase_ != PendingCopyAdmissionPhase::Collecting)
        return PendingCopyStatus::InvalidPhase;

    PendingCopyLedger *const ledger = receipt->ledger_;
    if (!ledger || !ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    if (ledger->phase_ != PendingCopyLedger::Phase::Ready
        || receipt->generationIndex_ + 1 != ledger->generationCount_)
    {
        return PendingCopyStatus::InvalidState;
    }
    if (!zone_load::ZoneLoadContextKeyMatches(receipt->lifecycle_, key)
        || receipt->lifecycle_->phase()
            != zone_load::ZoneLoadContextPhase::Loading)
    {
        return PendingCopyStatus::InvalidPhase;
    }
    PendingCopyLedger::GenerationDescriptor &descriptor =
        ledger->generations_[receipt->generationIndex_];
    if (descriptor.receipt != receipt || descriptor.key != key
        || descriptor.serial != receipt->generationSerial_
        || descriptor.phase
            != PendingCopyLedger::GenerationPhase::Collecting)
    {
        return PendingCopyStatus::UnsafeFailure;
    }

    receipt->completionContext_ = completion.context;
    receipt->completion_ = completion.complete;
    receipt->setPhase(PendingCopyAdmissionPhase::Prepared);
    descriptor.phase = PendingCopyLedger::GenerationPhase::Prepared;
    ledger->setPhase(PendingCopyLedger::Phase::AdmissionPrepared);
    return PendingCopyStatus::Success;
}

void FinalizePreparedPendingCopyAdmission(
    PendingCopyAdmissionReceipt &receipt) noexcept
{
    // Preparation has already completed the fallible whole-state preflight.
    // The phase check is solely the replay/reentry guard required around the
    // no-fail completion callback.
    if (receipt.phase_ != PendingCopyAdmissionPhase::Prepared)
        return;

    PendingCopyLedger &ledger = *receipt.ledger_;
    PendingCopyLedger::GenerationDescriptor &descriptor =
        ledger.generations_[receipt.generationIndex_];
    void *const completionContext = receipt.completionContext_;
    void (*const completion)(void *) noexcept = receipt.completion_;

    receipt.setPhase(PendingCopyAdmissionPhase::Admitting);
    descriptor.phase = PendingCopyLedger::GenerationPhase::Admitted;
    ledger.setPhase(PendingCopyLedger::Phase::Ready);
    ledger.callbackActive_ = 1;
    completion(completionContext);

    receipt.completionContext_ = nullptr;
    receipt.completion_ = nullptr;
    receipt.setPhase(PendingCopyAdmissionPhase::Admitted);
    ledger.callbackActive_ = 0;

    // A generation without copy records needs no later drain. Preparation
    // guarantees that the generation is the last descriptor, so removal is a
    // constant-time terminal publication.
    if (descriptor.recordCount == 0)
    {
        --ledger.generationCount_;
        ledger.generations_[ledger.generationCount_] = {};
        receipt.generationIndex_ = kInvalidGenerationIndex;
        receipt.setPhase(PendingCopyAdmissionPhase::Drained);
    }
}

PendingCopyStatus TryDiscardPendingCopyAdmission(
    PendingCopyAdmissionReceipt *const receipt,
    const zone_load::ZoneLoadContextKey &keyArgument) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!receipt)
        return PendingCopyStatus::InvalidArgument;
    if (!receipt->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    const PendingCopyStatus keyStatus =
        ValidateRequestedKey(receipt->key_, key);
    if (keyStatus != PendingCopyStatus::Success)
        return keyStatus;

    // Terminal receipt authority is checked before the ledger or lifecycle.
    // A stale retry therefore cannot inspect or mutate a newer generation.
    if (IsTerminalReceiptPhase(receipt->phase_))
        return PendingCopyStatus::AlreadyComplete;
    if (receipt->phase_ == PendingCopyAdmissionPhase::Admitting)
        return PendingCopyStatus::Busy;
    if (receipt->phase_ != PendingCopyAdmissionPhase::Collecting
        && receipt->phase_ != PendingCopyAdmissionPhase::Prepared
        && receipt->phase_ != PendingCopyAdmissionPhase::Admitted)
    {
        return PendingCopyStatus::InvalidPhase;
    }

    PendingCopyLedger *const ledger = receipt->ledger_;
    if (!ledger || !ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    if (ledger->phase_ == PendingCopyLedger::Phase::Draining)
        return PendingCopyStatus::Busy;
    if (!zone_load::ZoneLoadContextKeyMatches(receipt->lifecycle_, key))
        return PendingCopyStatus::StaleKey;
    const zone_load::ZoneLoadContextPhase lifecyclePhase =
        receipt->lifecycle_->phase();
    if (lifecyclePhase != zone_load::ZoneLoadContextPhase::Loading
        && lifecyclePhase != zone_load::ZoneLoadContextPhase::Live
        && lifecyclePhase != zone_load::ZoneLoadContextPhase::Abandoning)
    {
        return PendingCopyStatus::InvalidPhase;
    }
    if (receipt->generationIndex_ >= ledger->generationCount_)
        return PendingCopyStatus::UnsafeFailure;
    const std::uint32_t removeGeneration = receipt->generationIndex_;
    const PendingCopyLedger::GenerationDescriptor removed =
        ledger->generations_[removeGeneration];
    if (removed.receipt != receipt || removed.key != key
        || removed.serial != receipt->generationSerial_
        || (receipt->phase_ == PendingCopyAdmissionPhase::Collecting
            && removed.phase
                != PendingCopyLedger::GenerationPhase::Collecting)
        || (receipt->phase_ == PendingCopyAdmissionPhase::Prepared
            && removed.phase
                != PendingCopyLedger::GenerationPhase::Prepared)
        || (receipt->phase_ == PendingCopyAdmissionPhase::Admitted
            && removed.phase
                != PendingCopyLedger::GenerationPhase::Admitted))
    {
        return PendingCopyStatus::UnsafeFailure;
    }

    const std::uint32_t removeBegin = removed.firstRecord;
    const std::uint32_t removeEnd = removeBegin + removed.recordCount;
    for (std::uint32_t source = removeEnd;
        source < ledger->recordCount_;
        ++source)
    {
        ledger->records_[source - removed.recordCount] =
            ledger->records_[source];
    }
    const std::uint32_t newRecordCount =
        ledger->recordCount_ - removed.recordCount;
    for (std::uint32_t index = newRecordCount;
        index < ledger->recordCount_;
        ++index)
    {
        ledger->records_[index] = {};
    }

    for (std::uint32_t source = removeGeneration + 1;
        source < ledger->generationCount_;
        ++source)
    {
        PendingCopyLedger::GenerationDescriptor shifted =
            ledger->generations_[source];
        shifted.firstRecord -= removed.recordCount;
        ledger->generations_[source - 1] = shifted;
        shifted.receipt->generationIndex_ = source - 1;
    }
    --ledger->generationCount_;
    ledger->generations_[ledger->generationCount_] = {};
    ledger->recordCount_ = newRecordCount;
    if (receipt->phase_ == PendingCopyAdmissionPhase::Prepared)
        ledger->setPhase(PendingCopyLedger::Phase::Ready);

    receipt->completionContext_ = nullptr;
    receipt->completion_ = nullptr;
    receipt->generationIndex_ = kInvalidGenerationIndex;
    receipt->setPhase(PendingCopyAdmissionPhase::Discarded);
    return PendingCopyStatus::Success;
}

PendingCopyStatus TryBeginPendingCopyDrain(
    PendingCopyLedger *const ledger) noexcept
{
    if (!ledger)
        return PendingCopyStatus::InvalidArgument;
    if (!ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    if (ledger->phase_ == PendingCopyLedger::Phase::Draining)
        return PendingCopyStatus::Success;
    if (ledger->phase_ == PendingCopyLedger::Phase::AdmissionPrepared)
        return PendingCopyStatus::Busy;
    if (ledger->phase_ != PendingCopyLedger::Phase::Ready)
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->generationCount_ != 0
        && ledger->generations_[ledger->generationCount_ - 1].phase
            != PendingCopyLedger::GenerationPhase::Admitted)
    {
        return PendingCopyStatus::Busy;
    }
    if (ledger->recordCount_ == 0)
    {
        return ledger->generationCount_ == 0
            ? PendingCopyStatus::AlreadyComplete
            : PendingCopyStatus::UnsafeFailure;
    }

    ledger->drainCursor_ = 0;
    ledger->setPhase(PendingCopyLedger::Phase::Draining);
    return PendingCopyStatus::Success;
}

PendingCopyStatus TryDrainNextPendingCopy(
    PendingCopyLedger *const ledger,
    const PendingCopyDrainCallback &callback) noexcept
{
    if (!ledger || !callback.consume)
        return PendingCopyStatus::InvalidArgument;
    if (!ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    if (ledger->phase_ != PendingCopyLedger::Phase::Draining)
    {
        return ledger->phase_ == PendingCopyLedger::Phase::Ready
                && ledger->recordCount_ == 0
            ? PendingCopyStatus::AlreadyComplete
            : PendingCopyStatus::InvalidPhase;
    }
    if (ledger->drainCursor_ >= ledger->recordCount_)
        return PendingCopyStatus::AlreadyComplete;

    const PendingCopyRecord record =
        ledger->records_[ledger->drainCursor_];
    ledger->callbackActive_ = 1;
    const PendingCopyDrainCallbackStatus callbackStatus =
        callback.consume(callback.context, record);
    ledger->callbackActive_ = 0;

    switch (callbackStatus)
    {
    case PendingCopyDrainCallbackStatus::Success:
        ++ledger->drainCursor_;
        return PendingCopyStatus::Success;
    case PendingCopyDrainCallbackStatus::Retry:
        return PendingCopyStatus::Retry;
    case PendingCopyDrainCallbackStatus::UnsafeFailure:
    default:
        ledger->poison();
        return PendingCopyStatus::UnsafeFailure;
    }
}

PendingCopyStatus TryFinishPendingCopyDrain(
    PendingCopyLedger *const ledger) noexcept
{
    if (!ledger)
        return PendingCopyStatus::InvalidArgument;
    if (!ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    if (ledger->phase_ != PendingCopyLedger::Phase::Draining)
    {
        return ledger->phase_ == PendingCopyLedger::Phase::Ready
                && ledger->recordCount_ == 0
                && ledger->generationCount_ == 0
            ? PendingCopyStatus::AlreadyComplete
            : PendingCopyStatus::InvalidPhase;
    }
    if (ledger->drainCursor_ != ledger->recordCount_)
        return PendingCopyStatus::InvalidState;

    std::array<PendingCopyAdmissionReceipt *,
        kPendingCopyGenerationCapacity> receipts{};
    const std::uint32_t generationCount = ledger->generationCount_;
    for (std::uint32_t index = 0; index < generationCount; ++index)
        receipts[index] = ledger->generations_[index].receipt;

    for (std::uint32_t index = 0; index < ledger->recordCount_; ++index)
        ledger->records_[index] = {};
    for (std::uint32_t index = 0; index < generationCount; ++index)
        ledger->generations_[index] = {};
    ledger->recordCount_ = 0;
    ledger->generationCount_ = 0;
    ledger->drainCursor_ = 0;
    ledger->setPhase(PendingCopyLedger::Phase::Ready);

    for (std::uint32_t index = 0; index < generationCount; ++index)
    {
        receipts[index]->generationIndex_ = kInvalidGenerationIndex;
        receipts[index]->setPhase(PendingCopyAdmissionPhase::Drained);
    }
    return PendingCopyStatus::Success;
}

PendingCopyStatus TryResetPendingCopyAdmissionReceipt(
    PendingCopyAdmissionReceipt *const receipt,
    const zone_load::ZoneLoadContextKey &keyArgument) noexcept
{
    const zone_load::ZoneLoadContextKey key = keyArgument;
    if (!receipt)
        return PendingCopyStatus::InvalidArgument;
    if (!receipt->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    const PendingCopyStatus keyStatus =
        ValidateRequestedKey(receipt->key_, key);
    if (keyStatus != PendingCopyStatus::Success)
        return keyStatus;
    if (IsTerminalReceiptPhase(receipt->phase_))
    {
        // Terminal receipt authority is ledger-independent. In particular,
        // a newer generation may currently own callback-active authority in
        // the same ledger without blocking this exact stale receipt reset.
        receipt->reset();
        return PendingCopyStatus::Success;
    }
    if (receipt->phase_ == PendingCopyAdmissionPhase::Admitting)
        return PendingCopyStatus::Busy;

    PendingCopyLedger *const ledger = receipt->ledger_;
    if (!ledger || !ledger->isCanonical())
        return PendingCopyStatus::UnsafeFailure;
    if (ledger->callbackActive_ != 0)
        return PendingCopyStatus::Busy;
    return PendingCopyStatus::InvalidPhase;
}

#ifdef KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING
void PendingCopyLedgerTestAccess::SetNextGenerationSerial(
    PendingCopyLedger *const ledger,
    const std::uint64_t serial) noexcept
{
    if (ledger)
        ledger->nextGenerationSerial_ = serial;
}

void PendingCopyLedgerTestAccess::SetRecordCount(
    PendingCopyLedger *const ledger,
    const std::uint32_t count) noexcept
{
    if (ledger)
        ledger->recordCount_ = count;
}

void PendingCopyLedgerTestAccess::SetGenerationCount(
    PendingCopyLedger *const ledger,
    const std::uint32_t count) noexcept
{
    if (ledger)
        ledger->generationCount_ = count;
}

void PendingCopyLedgerTestAccess::SetDrainCursor(
    PendingCopyLedger *const ledger,
    const std::uint32_t cursor) noexcept
{
    if (ledger)
        ledger->drainCursor_ = cursor;
}

void PendingCopyLedgerTestAccess::SetLedgerPhaseWitness(
    PendingCopyLedger *const ledger,
    const std::uint8_t witness) noexcept
{
    if (ledger)
        ledger->phaseWitness_ = witness;
}

void PendingCopyLedgerTestAccess::SetLedgerReserved(
    PendingCopyLedger *const ledger,
    const std::uint8_t reserved) noexcept
{
    if (ledger)
        ledger->reserved_ = reserved;
}

void PendingCopyLedgerTestAccess::SetRecord(
    PendingCopyLedger *const ledger,
    const std::uint32_t index,
    const PendingCopyRecord &record) noexcept
{
    if (ledger && index < ledger->records_.size())
        ledger->records_[index] = record;
}

void PendingCopyLedgerTestAccess::SetDescriptorFirstRecord(
    PendingCopyLedger *const ledger,
    const std::uint32_t index,
    const std::uint32_t firstRecord) noexcept
{
    if (ledger && index < ledger->generations_.size())
        ledger->generations_[index].firstRecord = firstRecord;
}

void PendingCopyLedgerTestAccess::SetDescriptorRecordCount(
    PendingCopyLedger *const ledger,
    const std::uint32_t index,
    const std::uint32_t recordCount) noexcept
{
    if (ledger && index < ledger->generations_.size())
        ledger->generations_[index].recordCount = recordCount;
}

void PendingCopyLedgerTestAccess::SetDescriptorSerial(
    PendingCopyLedger *const ledger,
    const std::uint32_t index,
    const std::uint64_t serial) noexcept
{
    if (ledger && index < ledger->generations_.size())
        ledger->generations_[index].serial = serial;
}

void PendingCopyLedgerTestAccess::SetDescriptorReserved(
    PendingCopyLedger *const ledger,
    const std::uint32_t index,
    const std::uint8_t reserved) noexcept
{
    if (ledger && index < ledger->generations_.size())
        ledger->generations_[index].reserved[0] = reserved;
}

void PendingCopyLedgerTestAccess::SetReceiptGenerationIndex(
    PendingCopyAdmissionReceipt *const receipt,
    const std::uint32_t index) noexcept
{
    if (receipt)
        receipt->generationIndex_ = index;
}

void PendingCopyLedgerTestAccess::SetReceiptGenerationSerial(
    PendingCopyAdmissionReceipt *const receipt,
    const std::uint64_t serial) noexcept
{
    if (receipt)
        receipt->generationSerial_ = serial;
}

void PendingCopyLedgerTestAccess::SetReceiptPhaseWitness(
    PendingCopyAdmissionReceipt *const receipt,
    const std::uint8_t witness) noexcept
{
    if (receipt)
        receipt->phaseWitness_ = witness;
}

void PendingCopyLedgerTestAccess::SetReceiptReserved(
    PendingCopyAdmissionReceipt *const receipt,
    const std::uint8_t reserved) noexcept
{
    if (receipt)
        receipt->reserved_[0] = reserved;
}
#endif
} // namespace db::zone_pending_copy
