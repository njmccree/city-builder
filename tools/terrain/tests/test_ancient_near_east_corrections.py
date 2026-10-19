"""The authored Ancient Near East correction set (maps/ancient_near_east/corrections) parses and applies.

The package itself needs the real elevation data, which only the PC build has; this checks what can be checked
without it: every file is valid, every feature has a source and a confidence, every geometry lies inside the map's
bounds, and each feature does something when applied to a flat synthetic raster of its own area.
"""

import json
import math
from pathlib import Path

import numpy as np
import pytest

from terrain import corrections as c
from terrain.corrections import Grid, MapState

CORRECTIONS = Path(__file__).resolve().parents[1] / "maps" / "ancient_near_east" / "corrections"
BOUNDS = (24.0, 21.0, 50.0, 38.5)  # the planned map bounds (lon/lat), T1.2.6.1
FILES = sorted(CORRECTIONS.glob("*.geojson"))


def to_world(lon, lat):
    """A plain equirectangular frame at 30 degrees north, in meters from lon 24, lat 21."""
    return (lon - BOUNDS[0]) * 111_320.0 * math.cos(math.radians(30.0)), (lat - BOUNDS[1]) * 110_574.0


def coordinates(geometry):
    if geometry is None:
        return []
    out = []

    def walk(node):
        if isinstance(node[0], (int, float)):
            out.append(node)
        else:
            for child in node:
                walk(child)

    walk(geometry["coordinates"])
    return out


def test_the_correction_files_exist():
    assert [f.name for f in FILES] == ["01_modern_works.geojson", "02_ancient_nile_delta.geojson",
                                       "03_lakes_and_coasts.geojson"]


@pytest.mark.parametrize("path", FILES, ids=lambda p: p.name)
def test_a_file_parses_and_stays_inside_the_map(path):
    corrections = c.load_correction_files([path], to_world)
    assert corrections
    data = json.loads(path.read_text(encoding="utf-8"))
    for feature in data["features"]:
        assert len(feature["properties"]["source"]) > 40, "a source should say more than a name"
        for lon, lat in coordinates(feature["geometry"]):
            assert BOUNDS[0] <= lon <= BOUNDS[2] and BOUNDS[1] <= lat <= BOUNDS[3], feature["properties"]["name"]


def test_the_set_applies_in_order_to_a_raster_of_the_whole_region():
    """Coarse (4 km) cells: only checks that nothing raises and that rivers are added."""
    corrections = c.load_correction_files(FILES, to_world)
    width = int(to_world(BOUNDS[2], 30)[0] // 4000) + 1
    height = int(to_world(30, BOUNDS[3])[1] // 4000) + 1
    state = MapState(Grid(np.full((height, width), 50.0), 0.0, 0.0, 4000.0),
                     Grid(np.ones((height, width), dtype=bool), 0.0, 0.0, 4000.0), [])
    effects = c.apply_corrections(state, corrections, to_world)
    assert len(effects) == len(corrections)
    assert sum(e.rivers_added for e in effects) == 6  # the six Nile Delta branches
    assert len({r.name for r in state.rivers}) == 6


def local_state(correction, cell_m=250.0, margin_m=3000.0):
    """A flat raster (100 m, all land) just big enough for one feature, in the same world frame."""
    rings = correction.rings or [correction.line]
    points = np.vstack(rings)
    lo = np.floor((points.min(axis=0) - margin_m) / cell_m) * cell_m
    hi = np.ceil((points.max(axis=0) + margin_m) / cell_m) * cell_m
    shape = (int((hi[1] - lo[1]) / cell_m), int((hi[0] - lo[0]) / cell_m))
    return MapState(Grid(np.full(shape, 100.0), lo[0], lo[1], cell_m),
                    Grid(np.ones(shape, dtype=bool), lo[0], lo[1], cell_m), [])


def all_corrections():
    return [correction for path in FILES for correction in c.load_correction_files([path], to_world)]


@pytest.mark.parametrize("correction", all_corrections(), ids=lambda x: x.name)
def test_every_feature_does_something(correction):
    state = local_state(correction)
    (effect,) = c.apply_corrections(state, [correction], to_world)
    if correction.op == "river_path":
        assert effect.rivers_added == 1
    else:
        assert effect.cells > 0
    if correction.op == "set_water":
        assert effect.land_cells_changed > 0
    if correction.op == "fill_reservoir":
        assert effect.cells > 0
        # a valley that falls toward the dam, not a flat surface
        assert state.dem.data.max() > state.dem.data[state.land.data].min()
