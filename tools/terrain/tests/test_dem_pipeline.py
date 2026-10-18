"""The elevation steps end to end on synthetic data: a DEM tile and a land zip in the download cache."""

import json

import numpy as np
import pytest
from pyproj import Transformer

from fixtures import BOUNDS, SEA_EDGE_LON, elevation_at, seed_cache, write_map_json
from terrain import build_map
from terrain.csr import read_csr
from terrain.detail import SAMPLES, TILE_M
from terrain.dem import COPDEM_DATASET, copdem_tile_name
from terrain.package import list_package_files
from terrain.projection import world_frame

STEPS = "mosaic,reproject,corrections,region,detail"


@pytest.fixture
def build(tmp_path):
    """Seeds the cache, writes map.json and runs the steps offline. Returns a function giving (exit, out dir, frame)."""
    seed_cache(tmp_path / "cache")

    def run(steps=STEPS, **map_extra):
        map_file = write_map_json(tmp_path, **map_extra)
        out = tmp_path / "out"
        code = build_map.main([str(map_file), "--out", str(out), "--cache", str(tmp_path / "cache"),
                               "--steps", steps, "--offline"])
        return code, out, world_frame({"bounds": BOUNDS})

    return run


def lonlat(frame, x, z):
    return Transformer.from_crs(frame.crs, "EPSG:4326", always_xy=True).transform(frame.origin_x + x, frame.origin_y + z)


def test_the_steps_write_the_region_rasters(build, capsys):
    code, out, frame = build()
    assert code == 0, capsys.readouterr().err
    region = out / "region"
    assert sorted(p.name for p in region.glob("*.csr")) == [
        "elevation.csr", "elevation_max.csr", "elevation_min.csr", "land_mask.csr"]
    mean = read_csr(region / "elevation.csr")
    low = read_csr(region / "elevation_min.csr")
    high = read_csr(region / "elevation_max.csr")
    land = read_csr(region / "land_mask.csr")
    assert mean.data.dtype == np.int16 and mean.scale == 0.25
    assert land.data.dtype == np.uint8
    rows, cols = mean.data.shape
    assert (cols, rows) == (int(np.ceil(frame.width_m / 1000)), int(np.ceil(frame.height_m / 1000)))
    mean_m, low_m, high_m = mean.values(), low.values(), high.values()
    assert (low_m <= mean_m + 1e-6).all() and (mean_m <= high_m + 1e-6).all()

    # Land is east of the coast, sea west of it; the sea is never above 0 m.
    coast_x = frame.to_world()(SEA_EDGE_LON, 30.35)[0]
    for col in range(cols):
        x = (col + 0.5) * 1000.0
        if abs(x - coast_x) > 1500.0:
            assert bool(land.data[rows // 2, col]) == (x > coast_x)
    assert (mean_m[land.data == 0] <= 0.0).all()
    assert (mean_m[land.data == 1] > 0.0).all()

    # Row 0 is south and elevation matches the synthetic ground on land away from the coast.
    for col, row in ((20, 5), (30, 20), (35, 28)):
        lon, lat = lonlat(frame, (col + 0.5) * 1000.0, (row + 0.5) * 1000.0)
        assert land.data[row, col] == 1
        assert mean_m[row, col] == pytest.approx(float(elevation_at(lon, lat)), abs=6.0)


def test_the_steps_write_the_detail_tiles(build, capsys):
    code, out, frame = build()
    assert code == 0, capsys.readouterr().err
    files = sorted((out / "detail").glob("*.csr"))
    assert files
    tiles = {}
    for path in files:
        tx, ty = (int(n) for n in path.stem.split("_"))
        raster = read_csr(path)
        assert raster.data.shape == (SAMPLES, SAMPLES) and raster.data.dtype == np.int16
        assert raster.scale == pytest.approx(0.1) and raster.offset == round(raster.offset)
        tiles[(tx, ty)] = raster.values()
    # Heights are right: sample (i, j) of a tile sits at world (tx * 2048 + 32 i, ty * 2048 + 32 j).
    (tx, ty), values = next(iter(sorted(tiles.items(), key=lambda t: (t[0][1], t[0][0]))[len(tiles) // 2:]))
    for i, j in ((0, 0), (10, 40), (64, 64), (33, 5)):
        lon, lat = lonlat(frame, tx * TILE_M + i * 32.0, ty * TILE_M + j * 32.0)
        if lon > SEA_EDGE_LON + 0.01:
            assert values[j, i] == pytest.approx(float(elevation_at(lon, lat)), abs=0.5)
    # Shared edges agree (to the decode precision) with the neighbors.
    checked = 0
    for (tx, ty), values in tiles.items():
        east = tiles.get((tx + 1, ty))
        north = tiles.get((tx, ty + 1))
        if east is not None:
            assert values[:, SAMPLES - 1] == pytest.approx(east[:, 0], abs=1e-3)
            checked += 1
        if north is not None:
            assert values[SAMPLES - 1, :] == pytest.approx(north[0, :], abs=1e-3)
            checked += 1
    assert checked > 10


def test_tiles_are_exactly_those_that_touch_the_detail_region(build):
    code, out, frame = build()
    assert code == 0
    to_world = frame.to_world()
    corners = [to_world(lon, lat) for lon, lat in ((30.3, 30.3), (30.5, 30.4))]
    names = {p.stem for p in (out / "detail").glob("*.csr")}
    xs = [c[0] for c in corners]
    zs = [c[1] for c in corners]
    # Every tile that contains a corner of the region exists; none lies entirely outside its bounding box.
    for x, z in corners:
        assert f"{int(x // TILE_M)}_{int(z // TILE_M)}" in names
    for name in names:
        tx, ty = (int(n) for n in name.split("_"))
        assert (tx + 1) * TILE_M > min(xs) - 600 and tx * TILE_M < max(xs) + 600
        assert (ty + 1) * TILE_M > min(zs) - 600 and ty * TILE_M < max(zs) + 600


def test_previews_are_written_and_not_part_of_the_package(build):
    code, out, _ = build()
    assert code == 0
    previews = sorted(p.name for p in (out / "previews").glob("*.png"))
    assert "region.png" in previews and "detail_all.png" in previews
    assert any(p.startswith("detail_") and p != "detail_all.png" for p in previews)
    assert all(not name.startswith("previews/") for name in list_package_files(out))
    assert all(p.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n" for p in (out / "previews").glob("*.png"))


def test_the_vrt_is_left_beside_the_package_not_in_it(build):
    code, out, _ = build()
    assert code == 0
    assert (out.parent / "out.work" / "dem.vrt").is_file()
    assert not list(out.rglob("*.vrt"))


def test_the_same_inputs_give_identical_files(tmp_path):
    seed_cache(tmp_path / "cache")
    map_file = write_map_json(tmp_path)
    for name in ("a", "b"):
        assert build_map.main([str(map_file), "--out", str(tmp_path / name), "--cache", str(tmp_path / "cache"),
                               "--steps", STEPS, "--offline"]) == 0
    names = [p.relative_to(tmp_path / "a").as_posix() for p in sorted((tmp_path / "a").rglob("*.csr"))]
    assert names
    for name in names:
        assert (tmp_path / "a" / name).read_bytes() == (tmp_path / "b" / name).read_bytes(), name


def write_flatten(folder, elevation_m, lon_lat_box):
    (min_lon, min_lat), (max_lon, max_lat) = lon_lat_box
    ring = [[min_lon, min_lat], [max_lon, min_lat], [max_lon, max_lat], [min_lon, max_lat], [min_lon, min_lat]]
    path = folder / "corrections.geojson"
    path.write_text(json.dumps({"type": "FeatureCollection", "features": [{
        "type": "Feature", "geometry": {"type": "Polygon", "coordinates": [ring]},
        "properties": {"op": "flatten_to", "elevation_m": elevation_m, "source": "test", "confidence": "high",
                       "name": "a test plateau"}}]}), encoding="utf-8")
    return path.name


def test_corrections_change_both_the_region_and_the_detail_tiles(tmp_path):
    seed_cache(tmp_path / "cache")
    plateau = write_flatten(tmp_path, 321.0, ((30.40, 30.33), (30.47, 30.37)))
    map_file = write_map_json(tmp_path, corrections=[plateau])
    out = tmp_path / "out"
    code = build_map.main([str(map_file), "--out", str(out), "--cache", str(tmp_path / "cache"), "--steps", STEPS,
                           "--offline"])
    assert code == 0
    frame = world_frame({"bounds": BOUNDS})
    x, z = frame.to_world()(30.435, 30.35)
    # Region: the cell under the plateau is 321 m in the mean, the minimum and the maximum.
    col, row = int(x // 1000), int(z // 1000)
    for name in ("elevation", "elevation_min", "elevation_max"):
        assert read_csr(out / "region" / f"{name}.csr").values()[row, col] == pytest.approx(321.0, abs=0.25)
    # Detail: the sample nearest the middle of the plateau.
    tx, ty = int(x // TILE_M), int(z // TILE_M)
    i, j = round((x - tx * TILE_M) / 32.0), round((z - ty * TILE_M) / 32.0)
    values = read_csr(out / "detail" / f"{tx}_{ty}.csr").values()
    assert values[j, i] == pytest.approx(321.0, abs=0.1)
    # Outside the plateau the real (synthetic) ground is untouched: the east-most tile of that row of tiles.
    far_tx = max(int(p.stem.split("_")[0]) for p in (out / "detail").glob(f"*_{ty}.csr"))
    assert far_tx > tx
    lon, lat = lonlat(frame, far_tx * TILE_M + 32.0 * 32.0, ty * TILE_M + 32.0 * 32.0)
    assert lon > 30.47
    far = read_csr(out / "detail" / f"{far_tx}_{ty}.csr").values()[32, 32]
    assert far == pytest.approx(float(elevation_at(lon, lat)), abs=0.5)
    assert abs(far - 321.0) > 10.0


def test_missing_ocean_tiles_read_as_sea(tmp_path, capsys):
    # The bounds reach into tiles N29 and E031 that the cache knows do not exist.
    seed_cache(tmp_path / "cache")
    cache = tmp_path / "cache" / COPDEM_DATASET
    for lat, lon in ((29, 29), (29, 30), (29, 31), (30, 29), (30, 31), (31, 29), (31, 30), (31, 31)):
        (cache / f"{copdem_tile_name(lat, lon)}.tif.missing").write_text("not found at the source\n")
    bounds = {"min_lon": 30.0, "min_lat": 29.95, "max_lon": 30.6, "max_lat": 30.5}
    map_file = write_map_json(tmp_path, bounds=bounds, detail_regions=[])
    out = tmp_path / "out"
    code = build_map.main([str(map_file), "--out", str(out), "--cache", str(tmp_path / "cache"),
                           "--steps", "mosaic,reproject,region", "--offline"])
    assert code == 0, capsys.readouterr().err
    frame = world_frame({"bounds": bounds})
    mean = read_csr(out / "region" / "elevation.csr").values()
    land = read_csr(out / "region" / "land_mask.csr").data
    south = (np.arange(mean.shape[0]) + 0.5) * 1000.0 < frame.to_world()(30.3, 30.0)[1] - 2000.0
    assert south.any()
    assert (mean[south, :][land[south, :] == 0] <= 0.0).all()


def test_a_map_without_bounds_is_refused_by_the_steps_that_need_them(tmp_path, capsys):
    map_file = tmp_path / "map.json"
    map_file.write_text(json.dumps({"map_id": "test:map.x", "package": "x"}))
    code = build_map.main([str(map_file), "--out", str(tmp_path / "out"), "--cache", str(tmp_path / "cache"),
                           "--steps", "mosaic"])
    assert code == 1
    assert "needs 'bounds'" in capsys.readouterr().err


def test_reproject_before_mosaic_says_so(tmp_path, capsys):
    seed_cache(tmp_path / "cache")
    map_file = write_map_json(tmp_path)
    code = build_map.main([str(map_file), "--out", str(tmp_path / "out"), "--cache", str(tmp_path / "cache"),
                           "--steps", "reproject", "--offline"])
    assert code == 1
    assert "run the 'mosaic' step first" in capsys.readouterr().err


def test_offline_names_every_missing_download_before_any_work(tmp_path, capsys):
    map_file = write_map_json(tmp_path)
    code = build_map.main([str(map_file), "--out", str(tmp_path / "out"), "--cache", str(tmp_path / "empty"),
                           "--steps", "fetch,mosaic", "--offline"])
    assert code == 1
    err = capsys.readouterr().err
    assert "4 needed file(s) are not in the cache" in err
    assert copdem_tile_name(30, 30) in err and "ne_10m_land.zip" in err
    assert "ne_10m_rivers_lake_centerlines_scale_rank.zip" in err and "ne_10m_lakes.zip" in err
    assert not (tmp_path / "out").exists()


def test_a_bad_detail_region_is_reported(tmp_path, capsys):
    seed_cache(tmp_path / "cache")
    map_file = write_map_json(tmp_path, detail_regions=[[[30.3, 30.3], [30.4, 30.3], [30.4, 30.4]]])
    code = build_map.main([str(map_file), "--out", str(tmp_path / "out"), "--cache", str(tmp_path / "cache"),
                           "--steps", "mosaic,detail", "--offline"])
    assert code == 1
    assert "detail_regions[0] is not a valid polygon" in capsys.readouterr().err
