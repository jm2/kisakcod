cmake_minimum_required(VERSION 3.16)

# Voice packet framing source contracts (VOX-2a/2b/2c/3 companion).
#
# The Speex codec runs ONLY at the endpoints (client capture and client
# playback; see tests/voice_gate_tests.cpp for the codec-level pins). The wire
# format between client and server is container framing around opaque Speex
# frames, and every byte of that framing is part of the retail compatibility
# surface governed by docs/design/NET_STEAM18.md. These
# contracts pin the framing statements verbatim; any change must go through
# the compatibility process.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing voice framing source: ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing voice framing invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden voice framing regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_count SOURCE_VARIABLE NEEDLE EXPECTED_COUNT DESCRIPTION)
    set(_remaining "${${SOURCE_VARIABLE}}")
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty voice framing count needle (${DESCRIPTION})")
    endif()
    set(_count 0)
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected voice framing invariant count (${DESCRIPTION}): "
            "expected ${EXPECTED_COUNT}, found ${_count}")
    endif()
endfunction()

read_normalized("src/client_mp/cl_voice.cpp" _client)
read_normalized("src/server_mp/sv_voice_mp.cpp" _server)
read_normalized("src/server_mp/sv_snapshot_mp.cpp" _snapshot)
read_normalized("src/win32/win_voice.cpp" _winvoice)

# --- Client to server upload framing (CL_VoiceWriteMPPacket) -----------------
# Packet count byte, then per-packet size byte + opaque Speex frame bytes.
require_contains(_client
    "MSG_WriteByte(&msg, cl_voiceCommunication.voicePacketCount)"
    "client upload writes the packet-count byte first")
require_contains(_client
    "MSG_WriteByte(&msg, cl_voiceCommunication.voicePackets[voicePacket].dataSize)"
    "client upload writes the per-frame size byte")
require_contains(_client
    "cl_voiceCommunication.voicePackets[voicePacket].dataSize <= 0"
    "client upload rejects non-positive frame sizes")
require_contains(_client
    "cl_voiceCommunication.voicePackets[voicePacket].dataSize >= 0x10000"
    "client upload rejects oversized frames")
require_contains(_client
    "MSG_WriteData( &msg, cl_voiceCommunication.voicePackets[voicePacket].data, cl_voiceCommunication.voicePackets[voicePacket].dataSize);"
    "client upload writes opaque frame bytes, no codec-level reinterpretation")
forbid_contains(_client "speex_" "client upload never touches the codec")

# --- Server relay queue (SV_SourceVoicePacket / queue caps) ------------------
require_count(_server "client->voicePacketCount < 40" 1
    "server keeps the retail 40-packet per-client relay queue cap")
require_count(_server "memcpy(client->voicePackets[client->voicePacketCount].data, voicePacket->data, voicePacket->dataSize)" 1
    "server relays opaque frame bytes verbatim")
forbid_contains(_server "speex_" "server never decodes or re-encodes voice")

# --- Server to client download framing (SV_WriteVoiceDataToClient) -----------
require_contains(_snapshot
    "MSG_WriteByte(msg, client->voicePacketCount)"
    "download writes the packet-count byte first")
require_contains(_snapshot
    "MSG_WriteByte(msg, client->voicePackets[packet].talker)"
    "download writes the talker byte per packet")
require_contains(_snapshot
    "MSG_WriteByte(msg, client->voicePackets[packet].dataSize)"
    "download writes the per-frame size byte")
require_contains(_snapshot
    "client->voicePackets[packet].dataSize >= 0x10000"
    "download rejects oversized frames")
require_contains(_snapshot
    "MSG_WriteData(msg, client->voicePackets[packet].data, client->voicePackets[packet].dataSize)"
    "download writes opaque frame bytes")

# --- Client download parse (CL_ParseVoice) -----------------------------------
require_contains(_client
    "voicePacket.dataSize = MSG_ReadByte(msg)"
    "client reads the per-frame size byte")
require_contains(_client
    "voicePacket.dataSize > 256"
    "client bounds the per-frame size against the retail 256-byte limit")
require_contains(_client
    "voicePacket.talker >= 0x40u"
    "client bounds the talker id against the retail 64-slot limit")
require_contains(_client
    "MSG_ReadData(msg, voicePacket.data, voicePacket.dataSize)"
    "client reads opaque frame bytes")
require_contains(_client
    "CL_IsPlayerMuted(localClientNum, voicePacket.talker)"
    "client keeps the retail mute gate in front of playback")

# --- Playback driver walks per-frame declared lengths ------------------------
require_contains(_winvoice
    "data_len = Decode_Sample((char *)&data[v5], v3, out, g_frame_size);"
    "playback decodes one frame per declared length")
require_contains(_winvoice
    "v5 += v3;"
    "playback advances by the frame's declared wire length, not a fixed stride")
require_contains(_winvoice
    "Client_SendVoiceData(bytes, &enc_buffer[enc_buffer_pos]);"
    "capture hands encoded bytes straight to the send path")
require_contains(_winvoice
    "bytes = Encode_Sample((short*)dst, &enc_buffer[enc_buffer_pos], 4096 - enc_buffer_pos);"
    "capture encodes the ring-buffer frame in place")

# Contract mutation self-verification: each mutation below is a plausible
# regression and must be rejected by the checks above.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "download_talker_dropped")
        string(REPLACE
            "MSG_WriteByte(msg, client->voicePackets[packet].talker);"
            "" _snapshot "${_snapshot}")
    elseif(CONTRACT_MUTATION STREQUAL "download_size_byte_dropped")
        string(REPLACE
            "MSG_WriteByte(msg, client->voicePackets[packet].dataSize);"
            "" _snapshot "${_snapshot}")
    elseif(CONTRACT_MUTATION STREQUAL "upload_size_byte_dropped")
        string(REPLACE
            "MSG_WriteByte(&msg, cl_voiceCommunication.voicePackets[voicePacket].dataSize);"
            "" _client "${_client}")
    elseif(CONTRACT_MUTATION STREQUAL "relay_cap_raised")
        string(REPLACE
            "client->voicePacketCount < 40" "client->voicePacketCount < 41"
            _server "${_server}")
    elseif(CONTRACT_MUTATION STREQUAL "client_size_bound_loosened")
        string(REPLACE
            "voicePacket.dataSize > 256" "voicePacket.dataSize > 512"
            _client "${_client}")
    elseif(CONTRACT_MUTATION STREQUAL "playback_fixed_stride")
        string(REPLACE
            "data_len = Decode_Sample((char *)&data[v5], v3, out, g_frame_size);"
            "data_len = Decode_Sample((char *)&data[v5], g_frame_size, out, g_frame_size);"
            _winvoice "${_winvoice}")
    elseif(CONTRACT_MUTATION STREQUAL "server_side_decode")
        string(APPEND _server " Decode_Sample(0, 0, 0, 0);")
    elseif(CONTRACT_MUTATION STREQUAL "mute_gate_bypassed")
        string(REPLACE
            "CL_IsPlayerMuted(localClientNum, voicePacket.talker)"
            "false" _client "${_client}")
    else()
        message(FATAL_ERROR
            "Unknown voice framing contract mutation: '${CONTRACT_MUTATION}'")
    endif()
endif()

require_contains(_client
    "MSG_WriteByte(&msg, cl_voiceCommunication.voicePacketCount)"
    "client upload framing survives mutations")
require_count(_server "client->voicePacketCount < 40" 1
    "relay cap survives mutations")
require_contains(_client
    "voicePacket.dataSize > 256"
    "client size bound survives mutations")
require_contains(_client
    "CL_IsPlayerMuted(localClientNum, voicePacket.talker)"
    "mute gate survives mutations")
require_contains(_snapshot
    "MSG_WriteByte(msg, client->voicePackets[packet].talker)"
    "download talker byte survives mutations")
require_contains(_snapshot
    "MSG_WriteByte(msg, client->voicePackets[packet].dataSize)"
    "download size byte survives mutations")
require_contains(_client
    "MSG_WriteByte(&msg, cl_voiceCommunication.voicePackets[voicePacket].dataSize)"
    "upload size byte survives mutations")
forbid_contains(_server "speex_" "server codec exclusion survives mutations")
forbid_contains(_server "Decode_Sample(" "server decode exclusion survives mutations")
require_contains(_winvoice
    "data_len = Decode_Sample((char *)&data[v5], v3, out, g_frame_size);"
    "playback frame walk survives mutations")

if(NOT DEFINED CONTRACT_MUTATION OR CONTRACT_MUTATION STREQUAL "")
    foreach(_mutation IN ITEMS
        download_talker_dropped
        download_size_byte_dropped
        upload_size_byte_dropped
        relay_cap_raised
        client_size_bound_loosened
        playback_fixed_stride
        server_side_decode
        mute_gate_bypassed)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _mutation_result
            OUTPUT_VARIABLE _mutation_stdout
            ERROR_VARIABLE _mutation_stderr)
        if(_mutation_result EQUAL 0)
            message(STATUS "Mutation stdout: ${_mutation_stdout}")
            message(STATUS "Mutation stderr: ${_mutation_stderr}")
            message(FATAL_ERROR
                "Voice framing contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Voice framing source contract passed")
