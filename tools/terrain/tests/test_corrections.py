import json

import numpy as np
import pytest

from terrain import corrections as c
from terrain.corrections import CorrectionError, Grid, MapState, RiverLine


def to_world(lon, lat):
    """Test frame: 1 degree is 1,000 m, so a polygon written in 'degrees' lies on the 1 km grid."""
    return lon * 1000.0, lat * 1000.0


def make_state(width=20, height=20, elevation=100.0, land=True, cell_m=1000.0):
    dem = Grid(np.full((height, width), elevation, dtype=np.float64), 0.0, 0.0, cell_m)
    mask = Grid(np.full((height, width), land, dtype=bool), 0.0, 0.0, cell_m)
    return MapState(dem, mask, [])


def square(x0, z0, x1, z1):
    return {"type": "Polygon", "coordinates": [[[x0, z0], [x1, z0], [x1, z1], [x0, z1], [x0, z0]]]}


def feature(op, geometry, **properties):
    properties = {"op": op, "source": "a test", "confidence": "high", **properties}
    return {"type": "Feature", "geometry": geometry, "properties": properties}


def parse(*features, file="test.geojson"):
    return c.parse_features({"type": "FeatureCollection", "features": list(features)}, file, to_world)


def run(state, *features):
    return c.apply_corrections(state, parse(*features), to_world)


# --- reading ----------------------------------------------------------------------------------------------------


def test_a_feature_needs_source_and_confidence():
    with pytest.raises(CorrectionError, match="missing property 'source'"):
        parse({"type": "Feature", "geometry": square(1, 1, 3, 3),
               "properties": {"op": "set_land", "min_elevation_m": 1, "confidence": "low"}})
    with pytest.raises(CorrectionError, match="missing property 'source'"):
        parse(feature("set_land", square(1, 1, 3, 3), min_elevation_m=1, source="  "))
    with pytest.raises(CorrectionError, match="'confidence' must be one of low, medium, high"):
        parse(feature("set_land", square(1, 1, 3, 3), min_elevation_m=1, confidence="certain"))
    with pytest.raises(CorrectionError, match="'op' must be one of"):
        parse(feature("paint_it_red", square(1, 1, 3, 3)))


def test_errors_name_the_file_and_the_feature():
    with pytest.raises(CorrectionError, match=r"suez.geojson feature 1 \(Suez Canal\): missing property 'level_m'"):
        parse(feature("set_land", square(1, 1, 3, 3), min_elevation_m=1),
              feature("set_water", square(1, 1, 3, 3), name="Suez Canal"), file="suez.geojson")


def test_operation_properties_and_geometry_are_checked():
    with pytest.raises(CorrectionError, match="must be a number"):
        parse(feature("elevation_offset", square(1, 1, 3, 3), meters="five"))
    with pytest.raises(CorrectionError, match="'feather_m' must not be negative"):
        parse(feature("flatten_to", square(1, 1, 3, 3), elevation_m=5, feather_m=-1))
    with pytest.raises(CorrectionError, match="needs a Polygon geometry"):
        parse(feature("set_water", None, level_m=0))
    with pytest.raises(CorrectionError, match="must be a Polygon or MultiPolygon"):
        parse(feature("set_water", {"type": "Point", "coordinates": [1, 1]}, level_m=0))
    with pytest.raises(CorrectionError, match="not closed"):
        parse(feature("set_water", {"type": "Polygon", "coordinates": [[[0, 0], [2, 0], [2, 2], [0, 2], [0, 1]]]},
                      level_m=0))
    with pytest.raises(CorrectionError, match="river_path needs a LineString"):
        parse(feature("river_path", square(1, 1, 3, 3), name="x", width_m=10, depth_m=2))
    line = {"type": "LineString", "coordinates": [[1, 1], [2, 2], [3, 3]]}
    with pytest.raises(CorrectionError, match="one value per vertex"):
        parse(feature("river_path", line, name="x", width_m=[10, 20], depth_m=2))
    with pytest.raises(CorrectionError, match="'match' must be"):
        parse(feature("remove_river", None, match={"colour": "blue"}))
    with pytest.raises(CorrectionError, match="valley_interpolate"):
        parse(feature("fill_reservoir", square(1, 1, 3, 3), profile="cliff"))


def test_files_are_read_in_order(tmp_path):
    first, second = tmp_path / "a.geojson", tmp_path / "b.geojson"
    for path, op in ((first, "set_land"), (second, "set_water")):
        extra = {"min_elevation_m": 1} if op == "set_land" else {"level_m": 0}
        path.write_text(json.dumps({"type": "FeatureCollection", "features": [feature(op, square(1, 1, 3, 3), **extra)]}))
    assert [x.op for x in c.load_correction_files([second, first], to_world)] == ["set_water", "set_land"]
    with pytest.raises(CorrectionError, match="not valid JSON"):
        (tmp_path / "bad.geojson").write_text("{nope")
        c.load_correction_files([tmp_path / "bad.geojson"], to_world)
    with pytest.raises(CorrectionError, match="cannot read the file"):
        c.load_correction_files([tmp_path / "missing.geojson"], to_world)
    with pytest.raises(CorrectionError, match="not a GeoJSON FeatureCollection"):
        (tmp_path / "x.geojson").write_text("[]")
        c.load_correction_files([tmp_path / "x.geojson"], to_world)


# --- polygon geometry ---------------------------------------------------------------------------------------------


def test_even_odd_fill_leaves_holes():
    outer = np.array([[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], dtype=float)
    hole = np.array([[4, 4], [6, 4], [6, 6], [4, 6], [4, 4]], dtype=float)
    x, z = np.meshgrid(np.arange(0.5, 10), np.arange(0.5, 10))
    inside = c.points_in_rings(x, z, [outer, hole])
    assert inside.sum() == 100 - 4
    assert not inside[4, 4] and inside[1, 1]


def test_distance_to_the_edge():
    ring = np.array([[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], dtype=float)
    d = c.distance_to_rings(np.array([5.0, 1.0, 12.0]), np.array([5.0, 5.0, 5.0]), [ring])
    assert d.tolist() == [5.0, 1.0, 2.0]


# --- operations ---------------------------------------------------------------------------------------------------


def test_set_water_lowers_the_polygon_and_clears_land():
    state = make_state()
    effects = run(state, feature("set_water", square(2, 3, 6, 7), level_m=0, bed_depth_m=3))
    assert state.dem.data[4, 4] == -3.0 and not state.land.data[4, 4]
    assert state.dem.data[0, 0] == 100.0 and state.land.data[0, 0]
    assert state.land.data.sum() == 400 - 16
    assert effects[0].cells == 16 and effects[0].land_cells_changed == 16
    assert effects[0].mean_change_m == pytest.approx(-103.0)
    assert effects[0].area_km2 == pytest.approx(16.0)


def test_set_water_keeps_ground_that_is_already_lower():
    state = make_state(elevation=-50.0)
    run(state, feature("set_water", square(2, 2, 5, 5), level_m=0))
    assert state.dem.data[3, 3] == -50.0


def test_set_land_fills_a_canal():
    state = make_state(elevation=-2.0, land=False)
    run(state, feature("set_land", square(5, 0, 7, 20), min_elevation_m=1.5))
    assert state.dem.data[10, 5] == 1.5 and state.land.data[10, 5]
    assert state.dem.data[10, 0] == -2.0 and not state.land.data[10, 0]
    # ground that is already higher is not lowered
    state = make_state(elevation=40.0)
    run(state, feature("set_land", square(5, 0, 7, 20), min_elevation_m=1.5))
    assert state.dem.data[10, 5] == 40.0


def test_elevation_offset_with_and_without_feather():
    state = make_state()
    run(state, feature("elevation_offset", square(4, 4, 10, 10), meters=-20))
    assert state.dem.data[6, 6] == 80.0 and state.dem.data[0, 0] == 100.0

    state = make_state()
    run(state, feature("elevation_offset", square(2, 2, 18, 18), meters=-20, feather_m=4000))
    row = state.dem.data[10]
    assert row[10] == pytest.approx(80.0)               # the middle gets the whole offset
    assert row[2] > row[3] > row[4]                     # fading in from the edge
    assert 80.0 < row[2] < 100.0
    assert state.dem.data[10, 0] == 100.0               # outside is untouched


def test_flatten_to_pulls_the_ground_to_a_level():
    state = make_state()
    state.dem.data[:] = np.linspace(0, 200, 20)[None, :]  # a slope
    run(state, feature("flatten_to", square(5, 5, 15, 15), elevation_m=50))
    assert np.allclose(state.dem.data[10, 5:15], 50.0)
    assert state.dem.data[10, 1] == pytest.approx(200 / 19)
    state = make_state()
    run(state, feature("flatten_to", square(2, 2, 18, 18), elevation_m=0, feather_m=6000))
    assert state.dem.data[10, 10] == pytest.approx(0.0)
    assert 0.0 < state.dem.data[10, 3] < 100.0


def test_a_hole_in_a_polygon_is_not_changed():
    geometry = {"type": "Polygon", "coordinates": [
        [[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], [[4, 4], [6, 4], [6, 6], [4, 6], [4, 4]]]}
    state = make_state()
    run(state, feature("elevation_offset", geometry, meters=10))
    assert state.dem.data[5, 5] == 100.0 and state.dem.data[1, 1] == 110.0


def river(name, rid, points):
    points = np.array(points, dtype=float)
    return RiverLine(rid, name, points, np.full(len(points), 50.0), np.full(len(points), 4.0))


def test_remove_river_by_name_or_id():
    state = make_state()
    state.rivers = [river("Nile", "nile_1", [(1000, 1000), (2000, 2000)]),
                    river("Nile", "nile_2", [(3000, 3000), (4000, 4000)]),
                    river("Euphrates", "euphrates", [(5000, 5000), (6000, 6000)])]
    effects = run(state, feature("remove_river", None, match={"name": "Nile"}))
    assert [r.name for r in state.rivers] == ["Euphrates"]
    assert effects[0].rivers_removed == 2
    effects = run(state, feature("remove_river", None, match={"id": "euphrates"}))
    assert state.rivers == [] and effects[0].rivers_removed == 1
    effects = run(state, feature("remove_river", None, match={"id": "nothing"}))
    assert effects[0].rivers_removed == 0 and "no river" in effects[0].note


def test_remove_river_only_inside_a_polygon():
    state = make_state()
    state.rivers = [river("Nile", "nile", [(1000, 5000), (3000, 5000), (5000, 5000), (7000, 5000), (9000, 5000)])]
    run(state, feature("remove_river", square(4, 4, 6, 6), match={"name": "Nile"}))
    ids = sorted(r.id for r in state.rivers)
    assert ids == ["nile_1", "nile_2"]
    parts = sorted((r.points.shape[0], r.points[0, 0]) for r in state.rivers)
    assert parts == [(2, 1000.0), (2, 7000.0)]
    # a river the polygon does not touch is left whole
    state.rivers = [river("Nile", "nile", [(1000, 1000), (2000, 1000)])]
    run(state, feature("remove_river", square(4, 4, 6, 6), match={"name": "Nile"}))
    assert [r.id for r in state.rivers] == ["nile"]


def test_river_path_adds_and_replaces():
    state = make_state()
    line = {"type": "LineString", "coordinates": [[1, 1], [5, 5], [9, 5]]}
    effects = run(state, feature("river_path", line, name="Pelusiac branch", width_m=[80, 60, 40], depth_m=3, id="pel"))
    assert [r.id for r in state.rivers] == ["pel"]
    assert state.rivers[0].points.tolist() == [[1000.0, 1000.0], [5000.0, 5000.0], [9000.0, 5000.0]]
    assert state.rivers[0].width_m.tolist() == [80.0, 60.0, 40.0]
    assert state.rivers[0].depth_m.tolist() == [3.0, 3.0, 3.0]
    assert effects[0].rivers_added == 1 and effects[0].rivers_removed == 0
    # the same name replaces it
    line2 = {"type": "LineString", "coordinates": [[2, 2], [8, 8]]}
    effects = run(state, feature("river_path", line2, name="Pelusiac branch", width_m=30, depth_m=2))
    assert len(state.rivers) == 1 and state.rivers[0].points.shape == (2, 2)
    assert effects[0].rivers_removed == 1 and "replaced" in effects[0].note


def test_fill_reservoir_replaces_the_flat_surface_with_a_valley():
    state = make_state(width=40, height=12, elevation=100.0)  # the reservoir's flat water surface at 100 m
    state.land.data[:] = False
    lake = square(5, 3, 35, 9)                                 # 30 km long, 6 km wide, running west-east
    run(state, feature("fill_reservoir", lake, profile="valley_interpolate", downstream=[5, 6], upstream=[35, 6],
                       downstream_elevation_m=60, upstream_elevation_m=90, bank_rise_m=10, name="Lake"))
    axis = state.dem.data[6, 5:35]                             # along the middle (row 6 is z = 6.5 km)
    assert axis[0] < axis[-1]                                  # the valley floor rises upstream
    assert axis[0] == pytest.approx(60, abs=1.5) and axis[-1] == pytest.approx(90, abs=1.5)
    assert state.dem.data[8, 20] > state.dem.data[6, 20]       # and rises toward the banks
    assert state.land.data[6, 20] and state.land.data[8, 20]
    assert state.dem.data[0, 20] == 100.0 and not state.land.data[0, 20]  # outside the polygon: untouched


def test_fill_reservoir_samples_the_ground_when_no_elevations_are_given():
    state = make_state(width=40, height=12, elevation=100.0)
    state.dem.data[6, 4] = 70.0    # just below the dam
    state.dem.data[6, 36] = 95.0   # at the inflow
    run(state, feature("fill_reservoir", square(5, 3, 35, 9), profile="valley_interpolate", downstream=[4.5, 6.5],
                       upstream=[36.5, 6.5], bank_rise_m=0))
    assert state.dem.data[6, 6] < state.dem.data[6, 33]
    assert state.dem.data[6, 5] == pytest.approx(70 + (95 - 70) * (0.5 / 32), abs=1.0)


def test_fill_reservoir_can_carve_a_channel():
    state = make_state(width=40, height=12)
    run(state, feature("fill_reservoir", square(5, 3, 35, 9), profile="valley_interpolate", downstream=[5, 6.5],
                       upstream=[35, 6.5], downstream_elevation_m=60, upstream_elevation_m=60, bank_rise_m=10,
                       channel_depth_m=5, channel_width_m=2000))
    assert state.dem.data[6, 20] < state.dem.data[8, 20]
    assert state.dem.data[6, 20] == pytest.approx(55.0, abs=0.5)
    with pytest.raises(CorrectionError, match="same point"):
        run(state, feature("fill_reservoir", square(5, 3, 35, 9), profile="valley_interpolate", downstream=[5, 6],
                           upstream=[5, 6]))


def test_corrections_apply_in_file_then_feature_order():
    state = make_state()
    run(state, feature("set_water", square(2, 2, 8, 8), level_m=0), feature("set_land", square(4, 4, 6, 6), min_elevation_m=3))
    assert state.dem.data[5, 5] == 3.0 and state.land.data[5, 5]     # land again, applied second
    assert state.dem.data[2, 2] == -2.0 and not state.land.data[2, 2]
    state = make_state()
    run(state, feature("set_land", square(4, 4, 6, 6), min_elevation_m=3), feature("set_water", square(2, 2, 8, 8), level_m=0))
    assert state.dem.data[5, 5] == -2.0 and not state.land.data[5, 5]  # the other order gives the other result


def test_a_polygon_outside_the_grid_does_nothing():
    state = make_state()
    effects = run(state, feature("set_water", square(50, 50, 60, 60), level_m=0))
    assert effects[0].cells == 0 and (state.dem.data == 100.0).all()


def test_the_dem_and_the_land_mask_must_match():
    state = make_state()
    state.land = Grid(np.ones((3, 3), dtype=bool), 0.0, 0.0, 1000.0)
    with pytest.raises(ValueError, match="same shape"):
        c.apply_corrections(state, [], to_world)


def test_the_report_lists_every_feature():
    state = make_state()
    effects = run(state,
                  feature("set_water", square(2, 3, 6, 7), level_m=0, name="Gulf", source="Author 2001", confidence="low"),
                  feature("remove_river", None, match={"name": "Nile"}, name="Old river"))
    text = c.format_report(effects, "Test corrections")
    assert text.startswith("# Test corrections")
    assert "## 1. set_water: Gulf" in text and "## 2. remove_river: Old river" in text
    assert "Source: Author 2001" in text and "Confidence: **low**" in text
    assert "Area: 16.0 km²" in text and "Mean elevation change: -102.00 m" in text
    assert "Confidence: 1 low, 0 medium, 1 high." in text


def test_the_report_is_written_to_a_file(tmp_path):
    effects = run(make_state(), feature("set_water", square(2, 3, 6, 7), level_m=0))
    c.write_report(tmp_path / "sub" / "report.md", effects)
    assert (tmp_path / "sub" / "report.md").read_text(encoding="utf-8").startswith("# Historical corrections")
