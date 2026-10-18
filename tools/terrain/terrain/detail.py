"""The 32 m detail tiles of a map package (docs/architecture.md section 15.1, 15.2).

A detail tile is a square of ``TILE_M`` = 2,048 m on the world grid (tile (tx, ty) covers x from ``tx * 2048`` and
z from ``ty * 2048``) with ``SAMPLES`` x ``SAMPLES`` = 65 x 65 height samples, 32 m apart, at the tile's corners:
sample (i, j) is at world ``(tx * 2048 + i * 32, ty * 2048 + j * 32)``. The samples on an edge are shared with the
neighbor tile, and the pipeline makes the neighbors agree on them (``reconcile_edges``).

On disk a tile is an i16 ``.csr`` raster: ``height = raw * 0.1 + offset``, with a whole-meter ``offset`` chosen per
tile so a 0.1 m step covers any relief a tile can have. Neighbors have different offsets, so a shared sample decodes
to the same height up to float rounding (well below a millimeter), not necessarily bit for bit.
"""

from __future__ import annotations

import math

import numpy as np
from shapely.geometry import Polygon, box

TILE_M = 2048.0
SPACING_M = 32.0
SAMPLES = 65          # TILE_M / SPACING_M + 1
HALO = 8              # extra samples read around a tile, so the corrections see their surroundings
RESOLUTION_M = 0.1    # height step of the stored i16
_I16_MAX = 32767


class DetailError(ValueError):
    """A tile cannot be stored (for example, relief too large for the 0.1 m step)."""


def tiles_for_polygons(polygons: list[Polygon], world_size: tuple[float, float] | None = None
                       ) -> list[tuple[int, int]]:
    """Every tile (tx, ty) whose square touches one of the polygons (world meters), sorted by (ty, tx). Tiles
    south or west of the world origin are dropped, and with ``world_size`` so are tiles entirely beyond it."""
    tiles: set[tuple[int, int]] = set()
    for polygon in polygons:
        min_x, min_z, max_x, max_z = polygon.bounds
        for ty in range(max(math.floor(min_z / TILE_M), 0), math.floor(max_z / TILE_M) + 1):
            for tx in range(max(math.floor(min_x / TILE_M), 0), math.floor(max_x / TILE_M) + 1):
                if world_size is not None and (tx * TILE_M >= world_size[0] or ty * TILE_M >= world_size[1]):
                    continue
                if polygon.intersects(box(tx * TILE_M, ty * TILE_M, (tx + 1) * TILE_M, (ty + 1) * TILE_M)):
                    tiles.add((tx, ty))
    return sorted(tiles, key=lambda t: (t[1], t[0]))


def tile_window(tx: int, ty: int) -> tuple[float, float, int]:
    """(x0, z0, n): the south-west corner and size in cells of the 32 m grid to read for a tile, so that the cell
    centers fall on the tile's samples and ``HALO`` extra cells surround it. Cell ``(HALO + i, HALO + j)`` is
    sample (i, j)."""
    return tx * TILE_M - (HALO + 0.5) * SPACING_M, ty * TILE_M - (HALO + 0.5) * SPACING_M, SAMPLES + 2 * HALO


def center(window: np.ndarray) -> np.ndarray:
    """The 65 x 65 samples of a tile out of its window (``[row, column]`` = ``[j, i]``)."""
    return window[HALO:HALO + SAMPLES, HALO:HALO + SAMPLES]


def reconcile_edges(tiles: dict[tuple[int, int], np.ndarray]) -> None:
    """Makes shared edge samples identical, in place. A sample on an edge takes the value from the first tile that
    has it in (ty, tx) order, so the result does not depend on how the tiles were produced. Arrays are
    ``[j, i]``: row j is the northing index."""
    last = SAMPLES - 1
    for tx, ty in sorted(tiles, key=lambda t: (t[1], t[0])):
        tile = tiles[(tx, ty)]
        left = tiles.get((tx - 1, ty))
        below = tiles.get((tx, ty - 1))
        below_left = tiles.get((tx - 1, ty - 1))
        if left is not None:
            tile[:, 0] = left[:, last]
        if below is not None:
            tile[0, :] = below[last, :]
        if below_left is not None:
            tile[0, 0] = below_left[last, last]
        # The corner shared with the tile above and to the left is that tile's to take from this one.


def quantize_tile(heights: np.ndarray) -> tuple[np.ndarray, float]:
    """(raw i16 array, offset): ``height ~ raw * 0.1 + offset``. The offset is the whole-meter middle of the tile's
    heights."""
    lowest = float(np.min(heights))
    highest = float(np.max(heights))
    offset = float(round((lowest + highest) / 2.0))
    raw = np.rint((heights.astype(np.float64) - offset) / RESOLUTION_M)
    if raw.min() < -_I16_MAX or raw.max() > _I16_MAX:
        raise DetailError(f"the heights of a tile span {lowest:.1f} to {highest:.1f} m, more than a 0.1 m i16 holds")
    return raw.astype(np.int16), offset
