#pragma once

// The calling thread's DPI awareness: "per-monitor v2", "per-monitor",
// "system", "unaware" or "unknown". The client manifest declares per-monitor v2.
const char *Sys_DpiAwarenessName();
