"""The river and lake extraction: the pure pieces (flow graph, discharge, geometry) and the whole step on the
synthetic ground, offline."""

import json

import numpy as np
import pytest
from fixtures import seed_cache, write_map_json
from river_fixtures import seed_river_cache

from terrain import build_map
from terrain.corrections import RiverLine
from terrain.riverdem import SnapParams, densify, non_increasing, sample_bilinear, section_minimum, snap_to_channel
from terrain.rivermodel import (RiverError, RiverParams, assign_discharge, assign_orders, assign_width_depth,
                                build_reaches, group_rivers, river_documents, simplify_line, topological_order)

STEPS = "mosaic,reproject,corrections,region,detail,rivers"
# A square lake on the synthetic ground (lon/lat), clockwise like shapefile exteriors.
LAKE_RING = [(30.40, 30.22), (30.40, 30.28), (30.46, 30.28), (30.46, 30.22), (30.40, 30.22)]


def line(name, ident, points):
    pts = np.array(points, dtype=float)
    return RiverLine(ident, name, pts, np.full(len(pts), 50.0), np.full(len(pts), 3.0))


def reach_graph(lines, junction_m=600.0):
    reaches = build_reaches(lines, junction_m)
    for r in reaches:
        r.own_discharge = 100.0
    return reaches


def y_junction():
    # two upstream arms meet the start of a trunk at (0, 0); the trunk runs south
    return [line("A", "a", [(-5000, 5000), (0, 0)]), line("B", "b", [(5000, 5000), (0, 0)]),
            line("Trunk", "t", [(0, 0), (0, -9000)])]


# --- parameters -----------------------------------------------------------------------------------------------------


def test_defaults_and_bad_settings():
    p = RiverParams.from_json(None)
    assert p.min_discharge_m3s == 100.0 and p.width(100.0) == pytest.approx(72.0)
    assert RiverParams.from_json({"width": {"a": 10, "b": 0.4}}).width(1.0) == pytest.approx(10.0)
    with pytest.raises(RiverError, match="unknown setting"):
        RiverParams.from_json({"min_discharge": 5})
    with pytest.raises(RiverError, match="must be a number"):
        RiverParams.from_json({"min_discharge_m3s": "lots"})
    with pytest.raises(RiverError, match="rivers"):
        RiverParams.from_json([1])


# --- flow graph -----------------------------------------------------------------------------------------------------


def test_a_confluence_links_the_pieces():
    reaches = reach_graph(y_junction())
    by_line = {r.line_id.split(".")[0]: r for r in reaches}
    trunk = by_line["t"]
    assert sorted(by_line[k].key for k in "ab") == sorted(trunk.upstream)
    assert trunk.downstream == []
    assert by_line["a"].downstream == [trunk.key]
    order = topological_order(reaches)
    assert order.index(by_line["a"].key) < order.index(trunk.key)


def test_a_line_that_ends_on_another_cuts_it():
    main = line("Main", "m", [(0, 10000), (0, 5000), (0, 0), (0, -10000)])
    side = line("Side", "s", [(-6000, 3000), (-200, 0)])  # ends 200 m from the middle of Main
    reaches = reach_graph([main, side])
    assert len([r for r in reaches if r.line_id.startswith("m")]) == 2
    upper = next(r for r in reaches if r.line_id == "m.0")
    lower = next(r for r in reaches if r.line_id == "m.1")
    side_key = next(r.key for r in reaches if r.line_id.startswith("s"))
    assert sorted(lower.upstream) == sorted([upper.key, side_key])
    assert upper.points[-1][1] == pytest.approx(0.0, abs=1.0)  # cut at the foot of the side line


def test_a_bifurcation_splits_the_flow_equally():
    stem = line("Delta", "d", [(0, 10000), (0, 0)])
    left = line("Left", "l", [(0, 0), (-4000, -9000)])
    right = line("Right", "r", [(0, 0), (4000, -9000)])
    reaches = reach_graph([stem, left, right])
    for r in reaches:
        r.own_discharge = 1.0
    stem_r = next(r for r in reaches if r.line_id.startswith("d"))
    stem_r.own_discharge = 800.0
    assign_discharge(reaches)
    assert stem_r.discharge_m3s == pytest.approx(800.0)
    assert len(stem_r.downstream) == 2
    for r in reaches:
        if r is not stem_r:
            assert r.discharge_m3s == pytest.approx(400.0)


def test_explicit_discharge_is_kept_even_when_the_inflow_is_larger():
    a = line("A", "a", [(0, 5000), (0, 0)])
    b = line("B", "b", [(0, 0), (0, -5000)])
    reaches = reach_graph([a, b])
    ra = next(r for r in reaches if r.line_id.startswith("a"))
    rb = next(r for r in reaches if r.line_id.startswith("b"))
    ra.own_discharge = 500.0
    rb.own_discharge, rb.explicit_discharge = 50.0, True
    assign_discharge(reaches)
    assert rb.discharge_m3s == pytest.approx(50.0)


def test_strahler_order_grows_where_equal_orders_meet():
    reaches = reach_graph(y_junction())
    assign_discharge(reaches)
    assign_orders(reaches)
    assert {r.line_id.split(".")[0]: r.order for r in reaches} == {"a": 1, "b": 1, "t": 2}


def test_width_and_depth_follow_the_power_law_with_a_floor():
    reaches = reach_graph(y_junction())
    for r in reaches:
        r.own_discharge = 400.0
    assign_discharge(reaches)
    assign_width_depth(reaches, RiverParams())
    trunk = next(r for r in reaches if r.line_id.startswith("t"))
    assert trunk.width_m == pytest.approx(7.2 * trunk.discharge_m3s ** 0.5)
    assert trunk.depth_m == pytest.approx(0.27 * trunk.discharge_m3s ** 0.39)
    small = reach_graph([line("S", "s", [(0, 0), (0, -5000)])])
    small[0].own_discharge = 0.5
    assign_discharge(small)
    assign_width_depth(small, RiverParams())
    assert small[0].width_m == 10.0 and small[0].depth_m == 1.0


def test_groups_are_connected_components_and_documents_are_consistent():
    reaches = reach_graph(y_junction() + [line("Trunk", "f", [(40000, 0), (40000, -9000)])])
    groups = group_rivers(reaches)
    assert sorted(len(g) for g in groups) == [1, 3]
    for r in reaches:
        r.levels = np.linspace(10, 5, len(r.points))
        r.width_m = r.depth_m = 5.0
        r.floodplain = []
    docs = river_documents(reaches, groups)
    assert len({d["id"] for d in docs}) == 2 and len({d["name"] for d in docs}) == 2
    for d in docs:
        ids = [r["id"] for r in d["reaches"]]
        assert len(set(ids)) == len(ids)
        for r in d["reaches"]:
            assert all(x in ids for x in r["downstream"]) and r["id"] not in r["downstream"]


# --- geometry -------------------------------------------------------------------------------------------------------


def test_simplification_uses_the_tight_tolerance_only_inside_the_detail_area():
    from shapely.geometry import box

    xs = np.linspace(0, 10000, 201)
    wiggle = np.column_stack([xs, 100.0 * np.sin(xs / 800.0) + 40.0 * np.sin(xs / 60.0)])
    inside = simplify_line(wiggle, box(-1, -1000, 20000, 1000), 30.0, 300.0)
    outside = simplify_line(wiggle, None, 30.0, 300.0)
    assert len(inside) > len(outside) >= 2
    assert np.allclose(inside[0], wiggle[0]) and np.allclose(inside[-1], wiggle[-1])


def test_non_increasing_fit_removes_rises_without_dragging_everything_down():
    fitted = non_increasing(np.array([10.0, 9.0, 9.5, 8.0, 3.0, 4.0, 2.0]))
    assert (np.diff(fitted) <= 1e-9).all()
    assert fitted[0] == 10.0 and fitted[-1] == pytest.approx(2.0, abs=0.01) and fitted[3] > fitted[4]
    assert non_increasing(np.array([5.0, 4.0, 3.0])).tolist() == [5.0, 4.0, 3.0]


def test_bilinear_sampling_interpolates():
    window = np.array([[0.0, 10.0], [20.0, 30.0]])
    assert sample_bilinear(window, 0.0, 0.0, 10.0, np.array([10.0]), np.array([10.0]))[0] == pytest.approx(15.0)


class TrenchDem:
    """Flat ground apart from a north-south trench around x = 500 m (8 m below the rest)."""

    def window(self, x0, z0, cols, rows, cell_m, resampling="bilinear"):
        xs = x0 + (np.arange(cols) + 0.5) * cell_m
        return np.tile(np.where(np.abs(xs - 500.0) < 100.0, 0.0, 8.0), (rows, 1))


def test_snap_pulls_a_line_into_the_trench_and_section_minimum_finds_its_floor():
    start = densify(np.array([[620.0, 0.0], [620.0, 6000.0]]), 60.0)
    snapped, offsets = snap_to_channel(start, TrenchDem(), SnapParams(radius_m=400.0, penalty_m_per_km=0.1))
    assert abs(np.median(snapped[:, 0]) - 500.0) < 110.0
    floor = section_minimum(snapped, np.full(len(snapped), 150.0), TrenchDem())
    assert (floor == 0.0).all()


# --- the step, end to end -------------------------------------------------------------------------------------------


@pytest.fixture
def build(tmp_path):
    seed_cache(tmp_path / "cache")

    def run(lakes=(), steps=STEPS, **map_extra):
        seed_river_cache(tmp_path / "cache", lakes)
        map_extra.setdefault("rivers", {"snap": {"enabled": False}})
        map_file = write_map_json(tmp_path, **map_extra)
        out = tmp_path / "out"
        code = build_map.main([str(map_file), "--out", str(out), "--cache", str(tmp_path / "cache"),
                               "--steps", steps, "--offline"])
        return code, out

    return run


def load(out):
    return json.loads((out / "rivers.json").read_text()), json.loads((out / "lakes.json").read_text())


def all_reaches(rivers):
    return {r["id"]: r for river in rivers["rivers"] for r in river["reaches"]}


def test_the_step_keeps_the_rivers_above_the_threshold(build, capsys):
    code, out = build()
    assert code == 0, capsys.readouterr().err
    rivers, lakes = load(out)
    assert rivers["format"] == 1 and lakes == {"format": 1, "lakes": []}
    names = {river["name"] for river in rivers["rivers"]}
    assert "Big River" in names and "Tiny Brook" not in names and "Lake Line" not in names
    assert any(r["name"] == "Side Creek" for r in all_reaches(rivers).values())
    assert (out / "previews" / "rivers.png").is_file()


def test_water_never_rises_downstream_and_links_are_valid(build):
    code, out = build()
    assert code == 0
    reaches = all_reaches(load(out)[0])
    for reach in reaches.values():
        levels = [v["water_level_m"] for v in reach["vertices"]]
        assert all(b <= a + 1e-6 for a, b in zip(levels, levels[1:]))
        assert all(v["width_m"] > 0 and v["depth_m"] > 0 for v in reach["vertices"])
        for d in reach["downstream"]:
            assert d in reaches and d != reach["id"]
            assert reaches[d]["vertices"][0]["water_level_m"] <= levels[-1] + 1e-6
    assert max(r["discharge_m3s"] for r in reaches.values()) >= 1000.0


def test_the_river_flows_downhill_on_the_ground_that_was_built(build):
    code, out = build()
    assert code == 0
    reach = max(all_reaches(load(out)[0]).values(), key=lambda r: r["discharge_m3s"])
    first, last = reach["vertices"][0], reach["vertices"][-1]
    assert first["water_level_m"] > last["water_level_m"] > 0.0
    assert first["x"] > last["x"]  # the synthetic ground falls to the south-west


def test_the_floodplain_is_a_ring_around_the_channel(build):
    code, out = build(rivers={"snap": {"enabled": False},
                              "floodplain": {"distance_m": 2000, "height_m": 80, "cell_m": 100, "min_area_km2": 0.1}})
    assert code == 0
    reach = max(all_reaches(load(out)[0]).values(), key=lambda r: r["discharge_m3s"])
    assert reach["floodplain"] and len(reach["floodplain"][0]) >= 4


def test_a_rerun_gives_identical_files(build):
    code, out = build()
    assert code == 0
    first = (out / "rivers.json").read_bytes(), (out / "lakes.json").read_bytes()
    code, out = build()
    assert code == 0 and ((out / "rivers.json").read_bytes(), (out / "lakes.json").read_bytes()) == first


def test_a_lake_becomes_a_polygon_with_a_level(build):
    code, out = build(lakes=[("Lake One", LAKE_RING)])
    assert code == 0
    lakes = load(out)[1]["lakes"]
    assert len(lakes) == 1 and lakes[0]["name"] == "Lake One"
    assert lakes[0]["area_km2"] > 20.0 and lakes[0]["water_level_m"] > 0
    assert len(lakes[0]["rings"][0]) >= 4


def test_small_lakes_are_dropped_and_reservoir_corrections_skip_the_rest(build, tmp_path):
    tiny = [(30.5, 30.2), (30.5, 30.205), (30.505, 30.205), (30.505, 30.2), (30.5, 30.2)]
    code, out = build(lakes=[("Pond", tiny)])
    assert code == 0 and load(out)[1]["lakes"] == []

    ring = [[30.38, 30.2], [30.48, 30.2], [30.48, 30.3], [30.38, 30.3], [30.38, 30.2]]
    overlay = {"type": "FeatureCollection", "features": [{
        "type": "Feature", "geometry": {"type": "Polygon", "coordinates": [ring]},
        "properties": {"op": "fill_reservoir", "source": "a test", "confidence": "medium", "name": "Dam", "profile": "valley_interpolate", "downstream": [30.3, 30.2], "upstream": [30.48, 30.3]}}]}
    (tmp_path / "dam.geojson").write_text(json.dumps(overlay))
    code, out = build(lakes=[("Lake One", LAKE_RING)], corrections=["dam.geojson"])
    assert code == 0 and load(out)[1]["lakes"] == []


def test_river_corrections_remove_and_add_rivers(build, tmp_path):
    overlay = {"type": "FeatureCollection", "features": [
        {"type": "Feature", "geometry": None,
         "properties": {"op": "remove_river", "source": "a test", "confidence": "high", "match": {"name": "Side Creek"}}},
        {"type": "Feature",
         "geometry": {"type": "LineString", "coordinates": [[30.5, 30.45], [30.4, 30.3], [30.33, 30.25]]},
         "properties": {"op": "river_path", "source": "a test", "confidence": "medium", "name": "New Cut",
                        "width_m": 40, "depth_m": 3}}]}
    (tmp_path / "rivers.geojson").write_text(json.dumps(overlay))
    code, out = build(corrections=["rivers.geojson"])
    assert code == 0
    reaches = all_reaches(load(out)[0])
    names = {r["name"] for r in reaches.values()}
    assert "Side Creek" not in names and "New Cut" in names and "Big River" in names
    assert (tmp_path / "out.reports" / "corrections_rivers.md").is_file()
    new = next(r for r in reaches.values() if r["name"] == "New Cut")
    assert new["vertices"][0]["width_m"] == 40.0


def test_a_per_river_override_sets_the_discharge(build):
    code, out = build(rivers={"snap": {"enabled": False}, "overrides": {
        "Big River": {"discharge_m3s": 2500, "source": "a test", "confidence": "low"}}})
    assert code == 0
    assert max(r["discharge_m3s"] for r in all_reaches(load(out)[0]).values()) >= 2500.0


def test_bad_river_settings_are_a_clean_error(build, capsys):
    code, _ = build(rivers={"min_discharge": 5})
    assert code == 1 and "unknown setting" in capsys.readouterr().err
