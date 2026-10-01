#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from db_disk32.schema

// SndCurve's 64-bit load (header slot, pointer step and record body) is
// generated from its schema entry. Only its check is hand-written.
bool db::disk32_load::CheckSndCurve(const SndCurve &curve)
{
    // Load_SndCurve's rules: 2 to 8 knots, normalized as a falloff graph.
    return db::validation::CountInRange(curve.knotCount, 2, 8)
        && db::validation::NormalizedGraphKnots(curve.knots, static_cast<std::uint32_t>(curve.knotCount));
}

void __cdecl DB_LoadSndCurvePtrDisk32(bool atStreamStart, SndCurve **slot)
{
    db::disk32_load::LoadSndCurveHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
