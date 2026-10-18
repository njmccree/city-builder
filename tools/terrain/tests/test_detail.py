import numpy as np
import pytest
from shapely.geometry import Polygon, box

from terrain import detail
from terrain.detail import (DetailError, SAMPLES, TILE_M, center, quantize_tile, reconcile_edges, tile_window,
                            tiles_for_polygons)


def test_tiles_are_the_squares_that_touch_the_polygon():
    # A polygon inside one tile.
    assert tiles_for_polygons([box(100, 100, 900, 900)]) == [(0, 0)]
    # One that straddles a tile corner touches four, sorted by row then column.
    assert tiles_for_polygons([box(2000, 2000, 2100, 2100)]) == [(0, 0), (1, 0), (0, 1), (1, 1)]
    # A triangle does not touch the tile its bounding box covers but it misses (its slanted side stays below
    # x + z = 4000, and the tile (1, 1) starts at x + z = 4096).
    triangle = Polygon([(0, 0), (4000, 0), (0, 4000)])
    assert (1, 1) not in tiles_for_polygons([triangle])
    assert (0, 1) in tiles_for_polygons([triangle]) and (1, 0) in tiles_for_polygons([triangle])


def test_tiles_beyond_the_world_are_dropped():
    big = box(-5000, -5000, 20000, 20000)
    tiles = tiles_for_polygons([big], world_size=(4096.0, 4096.0))
    assert tiles == [(0, 0), (1, 0), (0, 1), (1, 1)]  # nothing south or west of the origin, nothing past 4096


def test_two_polygons_share_tiles_once():
    tiles = tiles_for_polygons([box(100, 100, 200, 200), box(300, 300, 400, 400)])
    assert tiles == [(0, 0)]


def test_a_window_puts_cell_centers_on_the_samples():
    x0, z0, n = tile_window(3, 5)
    assert n == SAMPLES + 2 * detail.HALO
    # Cell (HALO + i) has its center on sample i: world x = tx * 2048 + i * 32.
    for i in (0, 1, 32, 64):
        assert x0 + (detail.HALO + i + 0.5) * detail.SPACING_M == 3 * TILE_M + i * detail.SPACING_M
        assert z0 + (detail.HALO + i + 0.5) * detail.SPACING_M == 5 * TILE_M + i * detail.SPACING_M
    window = np.arange(n * n).reshape(n, n)
    assert center(window).shape == (SAMPLES, SAMPLES)
    assert center(window)[0, 0] == window[detail.HALO, detail.HALO]


def make_tiles(coords, seed=1):
    rng = np.random.default_rng(seed)
    return {c: rng.uniform(0, 100, (SAMPLES, SAMPLES)).astype(np.float32) for c in coords}


def test_shared_edges_become_identical():
    tiles = make_tiles([(0, 0), (1, 0), (0, 1), (1, 1), (2, 1)])
    reconcile_edges(tiles)
    last = SAMPLES - 1
    assert (tiles[(0, 0)][:, last] == tiles[(1, 0)][:, 0]).all()   # east edge of (0,0) = west edge of (1,0)
    assert (tiles[(0, 0)][last, :] == tiles[(0, 1)][0, :]).all()   # north edge = south edge of the tile above
    assert (tiles[(1, 1)][:, last] == tiles[(2, 1)][:, 0]).all()
    # The corner shared by four tiles is one value.
    corner = {tiles[(0, 0)][last, last], tiles[(1, 0)][last, 0], tiles[(0, 1)][0, last], tiles[(1, 1)][0, 0]}
    assert len(corner) == 1


def test_reconciling_takes_the_value_of_the_first_tile_in_row_then_column_order():
    tiles = make_tiles([(0, 0), (1, 0)])
    first_east_edge = tiles[(0, 0)][:, SAMPLES - 1].copy()
    reconcile_edges(tiles)
    assert (tiles[(1, 0)][:, 0] == first_east_edge).all()  # the west tile wins; its own samples are unchanged


def test_reconciling_does_not_depend_on_the_order_tiles_were_added():
    a = make_tiles([(0, 0), (1, 0), (0, 1)])
    b = {c: a[c].copy() for c in reversed(list(a))}
    reconcile_edges(a)
    reconcile_edges(b)
    assert all((a[c] == b[c]).all() for c in a)


def test_a_lone_tile_is_unchanged():
    tiles = make_tiles([(4, 4)])
    before = tiles[(4, 4)].copy()
    reconcile_edges(tiles)
    assert (tiles[(4, 4)] == before).all()


def test_quantizing_uses_a_whole_meter_offset_and_0_1_m_steps():
    rng = np.random.default_rng(3)
    heights = rng.uniform(-12.0, 80.0, (SAMPLES, SAMPLES)).astype(np.float32)
    raw, offset = quantize_tile(heights)
    assert raw.dtype == np.int16 and offset == float(round(offset))
    decoded = raw.astype(np.float64) * 0.1 + offset
    assert np.abs(decoded - heights).max() <= 0.05 + 1e-4


def test_shared_samples_decode_the_same_whatever_the_offset():
    heights = np.full((SAMPLES, SAMPLES), 10.0, dtype=np.float32)
    other = heights + 300.0
    shared = np.float32(123.4567)
    heights[3, 4] = shared
    other[7, 9] = shared
    raw_a, off_a = quantize_tile(heights)
    raw_b, off_b = quantize_tile(other)
    assert off_a != off_b
    value_a = float(np.float32(0.1)) * int(raw_a[3, 4]) + off_a
    value_b = float(np.float32(0.1)) * int(raw_b[7, 9]) + off_b
    assert value_a == pytest.approx(value_b, abs=1e-3)


def test_relief_beyond_the_i16_range_is_refused():
    heights = np.zeros((SAMPLES, SAMPLES), dtype=np.float32)
    heights[0, 0] = 7000.0
    with pytest.raises(DetailError, match="more than a 0.1 m i16 holds"):
        quantize_tile(heights)
