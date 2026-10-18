import numpy as np
import pytest
import rasterio
from pyproj import Transformer
from rasterio.enums import Resampling

from fixtures import BOUNDS, elevation_at, write_dem_tile
from terrain.cache import Source
from terrain.dem import (COPDEM_DATASET, DemError, Reprojector, build_vrt, copdem_sources, copdem_tile_name,
                         copdem_tile_source, tiles_for_bounds)
from terrain.projection import world_frame

MAP = {"bounds": BOUNDS}


def test_tile_names_follow_the_copernicus_pattern():
    assert copdem_tile_name(30, 31) == "Copernicus_DSM_COG_10_N30_00_E031_00_DEM"
    assert copdem_tile_name(0, 5) == "Copernicus_DSM_COG_10_N00_00_E005_00_DEM"
    assert copdem_tile_name(-1, -122) == "Copernicus_DSM_COG_10_S01_00_W122_00_DEM"


def test_a_tile_source_has_the_aws_url_and_may_be_absent():
    source = copdem_tile_source(30, 31)
    assert source.dataset == COPDEM_DATASET
    assert source.name == "Copernicus_DSM_COG_10_N30_00_E031_00_DEM.tif"
    assert source.url == ("https://copernicus-dem-30m.s3.amazonaws.com/Copernicus_DSM_COG_10_N30_00_E031_00_DEM/"
                          "Copernicus_DSM_COG_10_N30_00_E031_00_DEM.tif")
    assert source.optional  # open-sea tiles do not exist


def test_pins_from_map_json_become_size_and_checksum():
    pins = {"Copernicus_DSM_COG_10_N30_00_E031_00_DEM.tif": {"size": 123, "sha256": "ab" * 32}}
    source = copdem_tile_source(30, 31, pins)
    assert (source.size, source.sha256) == (123, "ab" * 32)
    assert copdem_tile_source(30, 30, pins).size is None


def test_tiles_cover_the_bounds_plus_a_margin():
    # Inside one tile with room to spare: just that tile.
    assert tiles_for_bounds((30.2, 30.2, 30.6, 30.5)) == [(30, 30)]
    # The Nile Delta apex box reaches over four tiles once the margin is added.
    assert tiles_for_bounds((30.8, 29.8, 31.4, 30.3)) == [(29, 30), (29, 31), (30, 30), (30, 31)]
    # Southern and western hemispheres floor toward the pole of the corner.
    assert tiles_for_bounds((-0.5, -0.5, 0.5, 0.5), 0.0) == [(-1, -1), (-1, 0), (0, -1), (0, 0)]
    assert len(copdem_sources((30.8, 29.8, 31.4, 30.3))) == 4


def test_the_mosaic_joins_neighboring_tiles(tmp_path):
    west = write_dem_tile(tmp_path / "west.tif", 30, 30, pixels=120)
    east = write_dem_tile(tmp_path / "east.tif", 30, 31, pixels=120)
    build_vrt([east, west], tmp_path / "dem.vrt")  # the order of the list does not matter
    with rasterio.open(tmp_path / "dem.vrt") as vrt:
        assert (vrt.width, vrt.height) == (240, 120)
        assert vrt.bounds.left == 30 and vrt.bounds.right == 32 and vrt.bounds.bottom == 30 and vrt.bounds.top == 31
        data = vrt.read(1)
    # The seam is invisible: elevation rises smoothly from the west tile into the east tile.
    centers = lambda col: 30.0 + (col + 0.5) / 120.0  # noqa: E731  (longitude of a pixel center)
    lat = 31.0 - 60.5 / 120.0
    assert abs(float(data[60, 119]) - float(elevation_at(centers(119), lat))) < 0.01
    assert abs(float(data[60, 120]) - float(elevation_at(centers(120), lat))) < 0.01


def test_a_mosaic_needs_tiles_in_geographic_coordinates(tmp_path):
    with pytest.raises(DemError, match="no DEM tiles"):
        build_vrt([], tmp_path / "dem.vrt")
    flat = tmp_path / "projected.tif"
    from rasterio.transform import from_origin
    with rasterio.open(flat, "w", driver="GTiff", width=4, height=4, count=1, dtype="float32", crs="EPSG:3857",
                       transform=from_origin(0, 100, 10, 10)) as dst:
        dst.write(np.zeros((4, 4), dtype="float32"), 1)
    with pytest.raises(DemError, match="geographic"):
        build_vrt([flat], tmp_path / "dem.vrt")


def make_reprojector(tmp_path):
    tile = write_dem_tile(tmp_path / "tile.tif", 30, 30, pixels=720)
    frame = world_frame(MAP)
    return Reprojector(tile, frame), frame


def lonlat_of(frame, x, z):
    to_lonlat = Transformer.from_crs(frame.crs, "EPSG:4326", always_xy=True)
    return to_lonlat.transform(frame.origin_x + x, frame.origin_y + z)


def test_a_window_is_in_the_world_frame_with_row_0_at_the_south(tmp_path):
    reprojector, frame = make_reprojector(tmp_path)
    with reprojector:
        window = reprojector.window(10_000.0, 8_000.0, 20, 15, 500.0)
    assert window.shape == (15, 20) and window.dtype == np.float32
    for row, col in ((0, 0), (14, 19), (7, 3), (2, 17)):
        x = 10_000.0 + (col + 0.5) * 500.0
        z = 8_000.0 + (row + 0.5) * 500.0
        lon, lat = lonlat_of(frame, x, z)
        assert float(window[row, col]) == pytest.approx(float(elevation_at(lon, lat)), abs=0.5)
    # Northward (rows) and eastward (columns) both rise on the synthetic ground.
    assert window[14, 0] > window[0, 0] and window[0, 19] > window[0, 0]


def test_a_window_beyond_the_data_is_nan(tmp_path):
    reprojector, frame = make_reprojector(tmp_path)
    with reprojector:
        window = reprojector.window(-400_000.0, 0.0, 4, 4, 1000.0)  # far west of the tile
    assert np.isnan(window).all()


def test_resampling_methods_give_mean_min_and_max(tmp_path):
    reprojector, _ = make_reprojector(tmp_path)
    with reprojector:
        mean = reprojector.window(10_000.0, 8_000.0, 6, 6, 2000.0, Resampling.average)
        low = reprojector.window(10_000.0, 8_000.0, 6, 6, 2000.0, Resampling.min)
        high = reprojector.window(10_000.0, 8_000.0, 6, 6, 2000.0, Resampling.max)
    assert (low <= mean + 1e-3).all() and (mean <= high + 1e-3).all()
    assert (high - low).max() > 1.0  # the ground is not flat inside a 2 km cell
