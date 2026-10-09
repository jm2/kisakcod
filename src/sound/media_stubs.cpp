// Silent Miles and Bink for 64-bit and POSIX clients (KISAK_MEDIA_STUBS on
// Win64; every POSIX client).
//
// mss32.lib and binkw32.lib are 32-bit Windows only, so no 64-bit or POSIX
// process can link or load them. Until the OpenAL Soft and FFmpeg backends
// land, those builds link these definitions: every call the engine makes succeeds
// as a no-op or reports failure, so sound and cinematics stay off.
// - AIL_startup() fails, so MSS_Startup() takes its existing "Miles
//   initialization failed" path.
// - BinkOpen() fails, so R_Cinematic_StartPlayback falls back and gives up;
//   BinkGetError() stays empty, as R_Cinematic_CheckBinkError requires.
// The Win32 build links the real libraries and never compiles this file.

// Declare the functions without dllimport, so this file can define them.
// MSS_SPU_PROCESS also adds console-only driver fields, which nothing here
// reads.
#define MSS_SPU_PROCESS
#define __RADINEXE__
#include <msslib/mss.h>
#include <binklib/bink.h>


// Miles: no driver, no streams, no samples.
S32 AILCALL AIL_startup(void) { return 0; }
void AILCALL AIL_shutdown(void) { }
// The headers return char *; callers only print the text.
char *AILCALL AIL_last_error(void) { return const_cast<char *>("Miles is not available in this build"); }
char *AILCALL AIL_set_redist_directory(char const *) { return nullptr; }
SINTa AILCALL AIL_set_preference(U32, SINTa) { return 0; }
void AILCALL AIL_set_file_callbacks(AIL_file_open_callback, AIL_file_close_callback, AIL_file_seek_callback, AIL_file_read_callback) { }
HDIGDRIVER AILCALL AIL_open_digital_driver(U32, S32, S32, U32) { return nullptr; }
#if defined(_WIN32)
// Miles declares this only in its Win32 API (DirectSound output).
S32 AILCALL AIL_set_DirectSound_HWND(HDIGDRIVER, HWND) { return 0; }
#endif
S32 AILCALL AIL_digital_CPU_percent(HDIGDRIVER) { return 0; }
void AILCALL AIL_set_speaker_configuration(HDIGDRIVER, MSSVECTOR3D *, S32, F32) { }
MSSVECTOR3D *AILCALL AIL_speaker_configuration(HDIGDRIVER, S32 *, S32 *, F32 *, MSS_MC_SPEC *) { return nullptr; }
void AILCALL AIL_set_3D_distance_factor(HDIGDRIVER, F32) { }
void AILCALL AIL_set_3D_rolloff_factor(HDIGDRIVER, F32) { }
void AILCALL AIL_set_room_type(HDIGDRIVER, S32) { }
void AILCALL AIL_set_digital_master_reverb_levels(HDIGDRIVER, F32, F32) { }
S32 AILCALL AIL_enumerate_filters(HPROENUM *, HPROVIDER *, C8 **) { return 0; }
S32 AILCALL AIL_find_filter(C8 const *, HPROVIDER *) { return 0; }
HDRIVERSTATE AILCALL AIL_open_filter(HPROVIDER, HDIGDRIVER) { return 0; }
S32 AILCALL AIL_WAV_info(void const *, AILSOUNDINFO *) { return 0; }
S32 AILCALL AIL_size_processed_digital_audio(U32, U32, S32, AILMIXINFO const *) { return 0; }
S32 AILCALL AIL_process_digital_audio(void *, S32, U32, U32, S32, AILMIXINFO *) { return 0; }

HSAMPLE AILCALL AIL_allocate_sample_handle(HDIGDRIVER) { return nullptr; }
S32 AILCALL AIL_init_sample(HSAMPLE, S32) { return 0; }
S32 AILCALL AIL_set_sample_info(HSAMPLE, AILSOUNDINFO const *) { return 0; }
HPROVIDER AILCALL AIL_set_sample_processor(HSAMPLE, SAMPLESTAGE, HPROVIDER) { return 0; }
S32 AILCALL AIL_sample_stage_property(HSAMPLE, SAMPLESTAGE, C8 const *, S32, void *, void const *, void *) { return 0; }
U32 AILCALL AIL_sample_status(HSAMPLE) { return SMP_DONE; }
void AILCALL AIL_end_sample(HSAMPLE) { }
void AILCALL AIL_stop_sample(HSAMPLE) { }
void AILCALL AIL_resume_sample(HSAMPLE) { }
S32 AILCALL AIL_sample_playback_rate(HSAMPLE) { return 0; }
void AILCALL AIL_set_sample_playback_rate(HSAMPLE, S32) { }
S32 AILCALL AIL_sample_channel_count(HSAMPLE, U32 *) { return 0; }
void AILCALL AIL_sample_channel_levels(HSAMPLE, MSS_SPEAKER const *, MSS_SPEAKER const *, F32 *, S32) { }
void AILCALL AIL_set_sample_channel_levels(HSAMPLE, MSS_SPEAKER const *, MSS_SPEAKER const *, F32 const *, S32) { }
void AILCALL AIL_sample_volume_levels(HSAMPLE, F32 *, F32 *) { }
void AILCALL AIL_set_sample_volume_levels(HSAMPLE, F32, F32) { }
void AILCALL AIL_sample_volume_pan(HSAMPLE, F32 *, F32 *) { }
void AILCALL AIL_set_sample_reverb_levels(HSAMPLE, F32, F32) { }
void AILCALL AIL_set_sample_loop_count(HSAMPLE, S32) { }
void AILCALL AIL_sample_ms_position(HSAMPLE, S32 *, S32 *) { }
void AILCALL AIL_set_sample_ms_position(HSAMPLE, S32) { }
S32 AILCALL AIL_sample_3D_position(HSAMPLE, F32 *, F32 *, F32 *) { return 0; }
void AILCALL AIL_set_sample_3D_position(HSAMPLE, F32, F32, F32) { }
void AILCALL AIL_set_sample_3D_distances(HSAMPLE, F32, F32, S32) { }

HSTREAM AILCALL AIL_open_stream(HDIGDRIVER, char const *, S32) { return nullptr; }
void AILCALL AIL_close_stream(HSTREAM) { }
void AILCALL AIL_pause_stream(HSTREAM, S32) { }
S32 AILCALL AIL_stream_status(HSTREAM) { return SMP_DONE; }
HSAMPLE AILCALL AIL_stream_sample_handle(HSTREAM) { return nullptr; }
void AILCALL AIL_stream_info(HSTREAM, S32 *, S32 *, S32 *, S32 *) { }
void AILCALL AIL_set_stream_loop_count(HSTREAM, S32) { }
void AILCALL AIL_stream_ms_position(HSTREAM, S32 *, S32 *) { }
void AILCALL AIL_set_stream_ms_position(HSTREAM, S32) { }

// Bink: no movie ever opens.
HBINK RADEXPLINK BinkOpen(const char *, U32) { return nullptr; }
// R_Cinematic_CheckBinkError asserts that BinkGetError() is null or empty.
char *RADEXPLINK BinkGetError(void) { return const_cast<char *>(""); }
BINKSNDOPEN RADEXPLINK BinkOpenMiles(UINTa) { return nullptr; }
S32 RADEXPLINK BinkSetSoundSystem(BINKSNDSYSOPEN, UINTa) { return 0; }
void RADEXPLINK BinkSetSoundTrack(U32, U32 *) { }
void RADEXPLINK BinkSetMemory(BINKMEMALLOC, BINKMEMFREE) { }
void RADEXPLINK BinkSetIOSize(U32) { }
void RADEXPLINK BinkClose(HBINK) { }
S32 RADEXPLINK BinkDoFrame(HBINK) { return 0; }
void RADEXPLINK BinkNextFrame(HBINK) { }
S32 RADEXPLINK BinkWait(HBINK) { return 0; }
S32 RADEXPLINK BinkPause(HBINK, S32) { return 0; }
S32 RADEXPLINK BinkControlBackgroundIO(HBINK, U32) { return 0; }
void RADEXPLINK BinkGetRealtime(HBINK, BINKREALTIME *, U32) { }
S32 RADEXPLINK BinkGetRects(HBINK, U32) { return 0; }
void RADEXPLINK BinkGetFrameBuffersInfo(HBINK, BINKFRAMEBUFFERS *) { }
void RADEXPLINK BinkRegisterFrameBuffers(HBINK, BINKFRAMEBUFFERS *) { }
void RADEXPLINK BinkSetMixBinVolumes(HBINK, U32, U32 *, S32 *, U32) { }
