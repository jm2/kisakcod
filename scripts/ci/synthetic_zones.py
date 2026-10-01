"""Synthetic empty fast files: the four zones a headless server loads in Com_Init.

Retail data never reaches CI (docs/ROADMAP.md). Each file written here is a
valid zone that holds no assets, so Com_Init completes and the server reaches
its frame loop. headless_smoke.py writes them at test time; they are never
committed.

The layout DB_LoadXFileInternal (src/database/db_file_load.cpp) reads:
  b'IWffu100'    magic of an unsigned fast file
  uint32 5       version
  a zlib stream of
    XFile        uint32 size, externalSize, blockSize[9]
    XAssetList   the 16-byte retail (disk32) record, read on every target:
                 int32 stringList.count, ptr32 stringList.strings,
                 int32 assetCount, ptr32 assets; all zero for an empty zone
"""

import struct
import zlib
from pathlib import Path

INIT_ZONES = ('code_post_gfx_mp', 'localized_code_post_gfx_mp', 'common_mp', 'localized_common_mp')
VERSION = 5
# Each of the nine zone blocks is allocated at this size; the empty list uses none of them.
BLOCK_SIZE = 0x10000


def empty_zone():
    """The bytes of a fast file whose asset list is empty."""
    xfile = struct.pack('<11I', 0, 0, *([BLOCK_SIZE] * 9))
    asset_list = struct.pack('<iIiI', 0, 0, 0, 0)
    return b'IWffu100' + struct.pack('<I', VERSION) + zlib.compress(xfile + asset_list)


def write_init_zones(zone_dir):
    """Write the four init zones into zone_dir, creating it if needed."""
    zone_dir = Path(zone_dir)
    zone_dir.mkdir(parents=True, exist_ok=True)
    for name in INIT_ZONES:
        (zone_dir / f'{name}.ff').write_bytes(empty_zone())
