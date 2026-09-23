// Census-only (scripts/ci/native64_census.py): force-included into the
// clearly labelled Win64 "probe link" only. It disables every static
// assertion so the link can report undefined symbols before the 64-bit size
// asserts are resolved. The probe never gates anything.
#pragma once
#ifdef __cplusplus
#define static_assert(...)
#else
#define _Static_assert(...)
#define static_assert(...)
#endif
