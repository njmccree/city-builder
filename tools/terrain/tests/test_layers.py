import json

import numpy as np
import pytest

from terrain import layers as L
from terrain.corrections import Grid, MapState, RiverLine
from terrain.layers import LayerError, LayerParams


def to_world(lon, lat):
    return lon * 1000.0, lat * 1000.0


def grid(values, cell_m=1000.0):
    return Grid(np.asarray(values), 0.0, 0.0, cell_m)


def square(x0, z0, x1, z1):
    return {"type": "Polygon", "coordinates": [[[x0, z0], [x1, z0], [x1, z1], [x0, z1], [x0, z0]]]}


def write_overlay(tmp_path, *features, name="o.geojson"):
    path = tmp_path / name
    path.write_text(json.dumps({"type": "FeatureCollection", "features": [
        {"type": "Feature", "geometry": g, "properties": {"source": "test", "confidence": "high", **p}}
        for g, p in features]}))
    return path


def river_world(size=30):
    """A flat desert with one river down column 10."""
    dem = grid(np.full((size, size), 50.0))
    land = grid(np.ones((size, size), dtype=bool))
    pts = np.array([[10500.0, 0.0], [10500.0, size * 1000.0]])
    river = RiverLine("r", "R", pts, np.array([30.0, 30.0]), np.array([3.0, 3.0]))
    return dem, land, [river]


def test_nearest_source_distances():
    src = np.zeros((9, 9), dtype=bool)
    src[4, 4] = True
    dist, _, _ = L.nearest_source(src, 100.0)
    assert dist[4, 4] == 0.0
    assert dist[4, 7] == pytest.approx(300.0)
    assert dist[0, 0] == pytest.approx(np.hypot(4, 4) * 100.0, rel=0.05)
    none, rows, _ = L.nearest_source(np.zeros((3, 3), dtype=bool), 1.0)
    assert np.isinf(none).all() and (rows == -1).all()


def test_fertility_follows_river_not_cropland():
    dem, land, rivers = river_world()
    cropland = grid(np.full(dem.shape, L.CROPLAND))
    out = L.derive_layers(dem, land, rivers, LayerParams(), landcover=cropland)
    near = out.fertility.data[15, 10]
    far = out.fertility.data[15, 28]
    assert near > 0.4
    assert far < 0.2  # modern cropland far from the river is not fertile
    assert out.wetness.data[15, 10] > out.wetness.data[15, 28]


def test_floodplain_raises_fertility_and_height_above_river_lowers_it():
    dem, land, rivers = river_world()
    plain = np.zeros(dem.shape, dtype=bool)
    plain[:, 13] = True
    base = L.derive_layers(dem, land, rivers, LayerParams()).fertility.data
    with_plain = L.derive_layers(dem, land, rivers, LayerParams(), floodplain=grid(plain)).fertility.data
    assert with_plain[15, 13] > base[15, 13] + 0.3
    high = grid(dem.data + np.where(np.arange(30) > 20, 200.0, 0.0)[None, :])
    raised = L.derive_layers(high, land, rivers, LayerParams()).fertility.data
    assert raised[15, 22] < base[15, 22]


def test_water_is_wet_and_barren():
    dem, land, rivers = river_world()
    land.data[:, :3] = False
    out = L.derive_layers(dem, land, rivers, LayerParams())
    assert (out.wetness.data[:, :3] == 1.0).all()
    assert (out.fertility.data[:, :3] == 0.0).all()
    assert (out.vegetation.data[:, :3] == 0.0).all()
    assert out.wetness.data[15, 3] > out.wetness.data[15, 25]  # the coast band


def test_wetland_landcover_is_wet():
    dem, land, _ = river_world()
    lc = np.full(dem.shape, L.BARE)
    lc[5, 25] = L.WETLAND
    out = L.derive_layers(dem, land, [], LayerParams(), landcover=grid(lc))
    assert out.wetness.data[5, 25] == pytest.approx(0.9)
    assert out.wetness.data[20, 25] < 0.9
    assert (0 <= out.fertility.data).all() and (out.fertility.data <= 1).all()


def test_shape_mismatch_is_an_error():
    dem, land, _ = river_world()
    with pytest.raises(LayerError, match="same shape"):
        L.derive_layers(dem, land, [], LayerParams(), floodplain=grid(np.zeros((3, 3), dtype=bool)))


def test_params_from_json():
    p = LayerParams.from_json({"river_weight": 0.5, "landcover_fertility": {"40": 0.9}})
    assert p.river_weight == 0.5 and p.landcover_fertility[L.CROPLAND] == 0.9
    assert p.landcover_fertility[L.TREE_COVER] == 0.40
    with pytest.raises(LayerError, match="unknown parameter"):
        LayerParams.from_json({"nope": 1})
    with pytest.raises(LayerError, match="greater than 0"):
        LayerParams.from_json({"noise_scale_m": 0})
    with pytest.raises(LayerError, match="number"):
        LayerParams.from_json({"river_weight": "x"})


def test_to_u8_and_layer_name():
    assert list(L.to_u8(np.array([-1.0, 0.0, 0.5, 1.0, 2.0]))) == [0, 0, 128, 255, 255]
    assert L.layer_name("base:resource.ore_copper") == "ore_copper"
    assert L.layer_name("base:ore_copper") == "ore_copper"
    assert L.layer_name("ore_copper") == "ore_copper"


def test_deposits_soft_edged_per_resource(tmp_path):
    path = write_overlay(tmp_path, (square(10, 10, 20, 20), {"resource": "base:resource.ore_copper", "richness": 0.8}))
    feats = L.load_deposits(path, to_world)
    template = grid(np.zeros((40, 40)))
    land = grid(np.ones((40, 40), dtype=bool))
    out = L.rasterize_deposits(template, land, feats, LayerParams(deposit_soft_edge_m=2000.0))
    assert list(out) == ["ore_copper"]
    layer = out["ore_copper"].data
    assert layer[15, 15] == pytest.approx(0.8)
    assert 0.0 <= layer[15, 21] < 0.8  # soft edge outside
    assert layer[15, 35] == 0.0
    land.data[15, 15] = False
    assert L.rasterize_deposits(template, land, feats, LayerParams())["ore_copper"].data[15, 15] == 0.0


def test_geology_gives_deterministic_generic_stone(tmp_path):
    path = write_overlay(tmp_path, (square(0, 0, 20, 20), {"geology": "limestone"}),
                         (square(20, 0, 40, 20), {"geology": "alluvium"}))
    feats = L.load_deposits(path, to_world)
    template = grid(np.zeros((40, 40)))
    land = grid(np.ones((40, 40), dtype=bool))
    a = L.rasterize_deposits(template, land, feats, LayerParams(), seed=7)
    b = L.rasterize_deposits(template, land, feats, LayerParams(), seed=7)
    assert list(a) == ["stone_limestone"]
    assert np.array_equal(a["stone_limestone"].data, b["stone_limestone"].data)
    data = a["stone_limestone"].data
    assert data[10, 10] > 0.25 and data[10, 30] == 0.0


def test_overlay_errors(tmp_path):
    geom = square(0, 0, 5, 5)
    no_source = tmp_path / "a.geojson"
    no_source.write_text(json.dumps({"type": "FeatureCollection", "features": [
        {"type": "Feature", "geometry": geom, "properties": {"geology": "limestone", "confidence": "high"}}]}))
    with pytest.raises(LayerError, match="source"):
        L.load_deposits(no_source, to_world)
    with pytest.raises(LayerError, match="confidence"):
        L.load_deposits(write_overlay(tmp_path, (geom, {"geology": "x", "confidence": "wild"}), name="b.geojson"),
                        to_world)
    with pytest.raises(LayerError, match="either"):
        L.load_deposits(write_overlay(tmp_path, (geom, {}), name="c.geojson"), to_world)
    with pytest.raises(LayerError, match="richness"):
        L.load_deposits(write_overlay(tmp_path, (geom, {"resource": "a:b", "richness": 2}), name="d.geojson"),
                        to_world)
    with pytest.raises(LayerError, match="cannot read"):
        L.load_deposits(tmp_path / "missing.geojson", to_world)


@pytest.mark.parametrize("mode,value,expected", [("set", 0.9, 0.9), ("add", 0.2, 0.5), ("mul", 2.0, 0.6)])
def test_fertility_override_modes(tmp_path, mode, value, expected):
    path = write_overlay(tmp_path, (square(5, 5, 10, 10), {"mode": mode, "value": value}))
    feats = L.load_fertility_overrides(path, to_world)
    fert = grid(np.full((20, 20), 0.3))
    L.apply_fertility_overrides(fert, feats)
    assert fert.data[7, 7] == pytest.approx(expected)
    assert fert.data[15, 15] == pytest.approx(0.3)


def test_fertility_override_feather_and_errors(tmp_path):
    path = write_overlay(tmp_path, (square(5, 5, 10, 10), {"mode": "set", "value": 1.0, "feather_m": 3000.0}))
    fert = grid(np.zeros((20, 20)))
    L.apply_fertility_overrides(fert, L.load_fertility_overrides(path, to_world))
    assert 0.0 < fert.data[5, 5] < fert.data[7, 7] < 1.0  # the feather fades in from the polygon edge
    assert fert.data[7, 12] == 0.0
    with pytest.raises(LayerError, match="mode"):
        L.load_fertility_overrides(write_overlay(tmp_path, (square(0, 0, 1, 1), {"mode": "x", "value": 1}),
                                                 name="e.geojson"), to_world)
    with pytest.raises(LayerError, match="between"):
        L.load_fertility_overrides(write_overlay(tmp_path, (square(0, 0, 1, 1), {"mode": "set", "value": 3}),
                                                 name="f.geojson"), to_world)
