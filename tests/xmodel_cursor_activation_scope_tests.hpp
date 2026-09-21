// xmodel_cursor_activation_scope_tests: checkpoint activation-scoping
// contracts for the checked seek (PR #140 rework, ki-okmr / #124).
//
// Organizational split only, mirroring xmodel_nested_cursor_walks.hpp:
// the activation-scoping contract section lives here so the suite TU
// stays within the file-size analyzer budget, and the
// TestSeekRejectsStaleActivationCheckpoints case groups are extracted
// into named stage helpers (per rejection class) so every function
// stays within the per-function budget — the ki-oh65/ki-dkeb stage
// split precedent, bodies moved verbatim. This header is included
// by exactly one TU, xmodel_nested_cursor_test.cpp, AFTER that TU
// defines its Checker instance and the CHECK macro these contracts
// evaluate through; no contract state is shared and no assertion is
// duplicated or weakened.
//
// Contracts (codex P2 at 60f6acd3): a Checkpoint carries the activation
// identity (a per-thread generation minted by every Activate) of the
// cursor that produced it, and SeekTo() rejects a foreign or stale
// checkpoint BEFORE applying the offset — through the same
// latched-Failed()/false path as an invalid or out-of-range checkpoint
// — so a checkpoint can never move a cursor other than the exact
// activation that produced it:
//   * a checkpoint captured before a Deactivate/Activate cycle is
//     rejected even when the next activation serves the SAME buffer or
//     a LARGER one whose window would admit the old offset (buffer
//     identity is not activation identity);
//   * a checkpoint captured while a nested child cursor was active is
//     rejected against the restored parent activation, while the
//     parent's own pre-nest checkpoint still seeks (no over-rejection);
//   * a rejected seek moves nothing (cursor position and anchored
//     *pos stay put) and a same-activation checkpoint with the very
//     same offset value into the same window is still accepted, so the
//     rejections turn on identity, not on the offset or window size.

#ifndef XMODEL_CURSOR_ACTIVATION_SCOPE_TESTS_HPP
#define XMODEL_CURSOR_ACTIVATION_SCOPE_TESTS_HPP

#include <xanim/buf_cursor.hpp>

#include "xmodel_cursor_test_support.hpp"

#include <cstdint>

namespace xmodel_nested_cursor_test
{
// TestSeekRejectsStaleActivationCheckpoints is split into named stages
// per rejection class (capture/teardown-zero, same-buffer re-activation
// mismatch, larger-fresh-window, positive control) so each helper stays
// within the per-function analyzer budget — the ki-oh65/ki-dkeb stage
// split precedent. Every CHECK below is byte-identical to the original
// single-body version; only the function boundaries moved.

// Stage: capture a checkpoint at top level, then tear the cursor stack
// back down to zero active cursors with a clean failure state. The
// returned checkpoint is stale against every later activation.
inline buf_cursor::Checkpoint CaptureStaleCheckpointTopLevelTeardownZero()
{
    unsigned char bufferA[8] = {};
    unsigned char *posA = bufferA;
    buf_cursor::Activate(bufferA, sizeof(bufferA));
    buf_cursor::AnchorPos(&posA);
    buf_cursor::Advance(4);
    const buf_cursor::Checkpoint staleCheckpoint = buf_cursor::Tell();
    CHECK(staleCheckpoint.valid);
    CHECK(staleCheckpoint.offset == 4);
    CHECK(staleCheckpoint.activation != 0);
    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
    CHECK(!buf_cursor::Failed());
    return staleCheckpoint;
}

// Stage (activation mismatch): the old checkpoint must not apply even
// though its offset (4) fits the re-activated same-size window exactly.
inline void RejectStaleCheckpointSameBufferReactivate(
    const buf_cursor::Checkpoint &staleCheckpoint)
{
    unsigned char bufferA[8] = {};
    // Same buffer re-activated: the generation has moved on, so the
    // old checkpoint must not apply even though its offset (4) fits
    // this window exactly. Buffer identity alone is not activation
    // identity.
    unsigned char *posA2 = bufferA;
    buf_cursor::Activate(bufferA, sizeof(bufferA));
    buf_cursor::AnchorPos(&posA2);
    CHECK(!buf_cursor::Failed());
    CHECK(!buf_cursor::SeekTo(staleCheckpoint));  // rejected
    CHECK(buf_cursor::Failed());                  // latched
    CHECK(buf_cursor::Tell().offset == 0);        // cursor did not move
    CHECK(posA2 == bufferA);                      // anchor did not move
    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
    CHECK(!buf_cursor::Failed());
}

// Stage (out-of-window identity): a fresh, LARGER window must reject
// the stale checkpoint too — the rejection is identity-driven, not
// size-driven.
inline void RejectStaleCheckpointLargerFreshWindow(
    const buf_cursor::Checkpoint &staleCheckpoint)
{
    unsigned char bufferB[16] = {};
    // Fresh activation over a LARGER buffer: the stale offset (4) fits
    // the 16-byte window a fortiori and must still be rejected — the
    // rejection is identity-driven, not size-driven.
    unsigned char *posB = bufferB;
    buf_cursor::Activate(bufferB, sizeof(bufferB));
    buf_cursor::AnchorPos(&posB);
    CHECK(!buf_cursor::Failed());
    CHECK(!buf_cursor::SeekTo(staleCheckpoint));
    CHECK(buf_cursor::Failed());
    CHECK(buf_cursor::Tell().offset == 0);
    CHECK(posB == bufferB);
    buf_cursor::Deactivate();
    CHECK(!buf_cursor::Failed());
}

// Stage (positive control): a checkpoint of the LIVE activation with
// the very same offset value is accepted, so the rejections above
// turned on activation identity, not on the offset value or window.
inline void AcceptOwnActivationCheckpointSameWindow(
    const buf_cursor::Checkpoint &staleCheckpoint)
{
    unsigned char bufferB[16] = {};
    // Positive control on the same 16-byte window: a checkpoint of
    // THIS activation with the very same offset value (4) is accepted,
    // and a hand-built same-activation checkpoint at offset 0 seeks
    // back too. The stale rejections above turned on activation
    // identity, not on the offset value or the window size.
    unsigned char *posB2 = bufferB;
    buf_cursor::Activate(bufferB, sizeof(bufferB));
    buf_cursor::AnchorPos(&posB2);
    buf_cursor::Advance(4);
    const buf_cursor::Checkpoint ownCheckpoint = buf_cursor::Tell();
    CHECK(ownCheckpoint.valid);
    CHECK(ownCheckpoint.offset == staleCheckpoint.offset);
    CHECK(buf_cursor::SeekTo(ownCheckpoint));
    CHECK(buf_cursor::Tell().offset == 4);
    CHECK(!buf_cursor::Failed());
    CHECK(buf_cursor::SeekTo(
        buf_cursor::Checkpoint{0, ownCheckpoint.activation, true}));
    CHECK(buf_cursor::Tell().offset == 0);
    CHECK(!buf_cursor::Failed());
    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
    CHECK(!buf_cursor::Failed());
}

// A checkpoint captured on one activation is stale after the matching
// Deactivate — even against the same buffer re-activated or a larger
// fresh buffer. SeekTo must reject it through the latched-failed path
// without moving the cursor or the anchored *pos, while a checkpoint
// of the live activation with the same offset still seeks.
inline bool TestSeekRejectsStaleActivationCheckpoints()
{
    const buf_cursor::Checkpoint staleCheckpoint =
        CaptureStaleCheckpointTopLevelTeardownZero();
    RejectStaleCheckpointSameBufferReactivate(staleCheckpoint);
    RejectStaleCheckpointLargerFreshWindow(staleCheckpoint);
    AcceptOwnActivationCheckpointSameWindow(staleCheckpoint);
    return true;
}

// A checkpoint captured while a nested child cursor is active belongs
// to the child activation: after the child window pops, SeekTo must
// reject it against the restored parent (same latched path, nothing
// moves). The parent's own pre-nest checkpoint must still seek after
// the pop — activation scoping must not over-reject the parent's own
// activation, which the pop legitimately resumes.
inline bool TestSeekRejectsNestedChildCheckpoint()
{
    unsigned char parentBuffer[12] = {};
    unsigned char childBuffer[8] = {};

    unsigned char *parentPos = parentBuffer;
    buf_cursor::Activate(parentBuffer, sizeof(parentBuffer));
    buf_cursor::AnchorPos(&parentPos);
    buf_cursor::Advance(2);
    const buf_cursor::Checkpoint parentCheckpoint = buf_cursor::Tell();
    CHECK(parentCheckpoint.valid);
    CHECK(parentCheckpoint.offset == 2);

    // Nested child window (the XModelPartsPrecache shape): its own
    // cursor over a different buffer; the parent's position, anchor
    // and failure state are preserved by the activation itself.
    unsigned char *childPos = childBuffer;
    buf_cursor::Activate(childBuffer, sizeof(childBuffer));
    buf_cursor::AnchorPos(&childPos);
    CHECK(!buf_cursor::Failed());
    buf_cursor::Advance(6);
    const buf_cursor::Checkpoint childCheckpoint = buf_cursor::Tell();
    CHECK(childCheckpoint.valid);
    CHECK(childCheckpoint.offset == 6);
    buf_cursor::Deactivate();

    // Parent restored exactly: the pop resumed the very activation
    // that produced parentCheckpoint, so that checkpoint still seeks.
    CHECK(buf_cursor::Current() != nullptr);
    CHECK(!buf_cursor::Failed());
    CHECK(buf_cursor::Tell().offset == 2);
    CHECK(parentPos == parentBuffer + 2);
    CHECK(buf_cursor::SeekTo(parentCheckpoint));
    CHECK(buf_cursor::Tell().offset == parentCheckpoint.offset);
    CHECK(parentPos == parentBuffer + parentCheckpoint.offset);
    CHECK(!buf_cursor::Failed());

    // The child's checkpoint is foreign here: offset 6 fits the
    // 12-byte parent window, so the rejection below is identity-
    // driven. It must reject BEFORE applying the offset, latch Failed
    // and move neither the cursor nor the anchored *pos.
    CHECK(!buf_cursor::SeekTo(childCheckpoint));
    CHECK(buf_cursor::Failed());
    CHECK(buf_cursor::Tell().offset == parentCheckpoint.offset);
    CHECK(parentPos == parentBuffer + parentCheckpoint.offset);

    buf_cursor::Deactivate();
    CHECK(buf_cursor::Current() == nullptr);
    CHECK(!buf_cursor::Failed());
    return true;
}
}  // namespace xmodel_nested_cursor_test

#endif  // XMODEL_CURSOR_ACTIVATION_SCOPE_TESTS_HPP
