import struct
import zlib

import numpy as np
import pytest

from fixtures import BOUNDS, SEA_EDGE_LON, write_land_zip
from terrain.landmask import LandMaskError, land_source, load_land_polygons, rasterize_land
from terrain.previews import hillshade, shaded_image, write_png
from terrain.projection import world_frame

MAP = {"bounds": BOUNDS}


def test_the_land_source_is_the_natural_earth_10m_land_zip():
    source = land_source()
    assert source.dataset == "natural-earth" and source.name == "ne_10m_land.zip"
    assert source.url == "https://naciscdn.org/naturalearth/10m/physical/ne_10m_land.zip"
    assert not source.optional
    assert land_source({"ne_10m_land.zip": {"size": 5, "sha256": "cd" * 32}}).sha256 == "cd" * 32


def test_polygons_are_cut_to_the_bounds(tmp_path):
    zip_path = write_land_zip(tmp_path / "land.zip", west_lon=29.0, east_lon=40.0)
    polygons = load_land_polygons(zip_path, (30.2, 30.2, 30.6, 30.5), margin_deg=0.25)
    assert len(polygons) == 1
    min_lon, min_lat, max_lon, max_lat = polygons[0].bounds
    assert (min_lon, min_lat, max_lon, max_lat) == pytest.approx((29.95, 29.95, 30.85, 30.75))


def test_polygons_outside_the_bounds_are_ignored(tmp_path):
    zip_path = write_land_zip(tmp_path / "land.zip", west_lon=20.0, east_lon=21.0)
    assert load_land_polygons(zip_path, (30.2, 30.2, 30.6, 30.5)) == []


def test_an_unreadable_land_file_is_a_clear_error(tmp_path):
    (tmp_path / "land.zip").write_bytes(b"not a zip")
    with pytest.raises(LandMaskError, match="cannot read the land polygons"):
        load_land_polygons(tmp_path / "land.zip", (30.2, 30.2, 30.6, 30.5))


def test_land_is_where_the_polygon_is_and_row_0_is_south(tmp_path):
    frame = world_frame(MAP)
    polygons = load_land_polygons(write_land_zip(tmp_path / "land.zip"), frame.bounds)
    width, height, cell = int(frame.width_m // 1000), int(frame.height_m // 1000), 1000.0
    land = rasterize_land(polygons, frame, 0.0, 0.0, width, height, cell)
    assert land.shape == (height, width) and land.dtype == bool
    coast_x = frame.to_world()(SEA_EDGE_LON, 30.35)[0]
    for col in range(width):
        center_x = (col + 0.5) * cell
        if abs(center_x - coast_x) > 1500.0:
            assert land[height // 2, col] == (center_x > coast_x), f"column {col}"
    # North-south: the polygon spans the whole height, so every row has land in the east.
    assert land[:, -1].all() and not land[:, 0].any()


def test_no_polygons_is_all_sea():
    frame = world_frame(MAP)
    assert not rasterize_land([], frame, 0.0, 0.0, 5, 4, 1000.0).any()


def test_a_finer_grid_finds_the_coast_where_the_coarse_one_does(tmp_path):
    frame = world_frame(MAP)
    polygons = load_land_polygons(write_land_zip(tmp_path / "land.zip"), frame.bounds)
    coarse = rasterize_land(polygons, frame, 0.0, 0.0, 30, 10, 1000.0)
    fine = rasterize_land(polygons, frame, 0.0, 0.0, 300, 100, 100.0)
    first_coarse = int(np.argmax(coarse[5]))
    first_fine = int(np.argmax(fine[50]))
    assert abs(first_fine * 100.0 - first_coarse * 1000.0) <= 1000.0


def decode_png(blob: bytes) -> np.ndarray:
    assert blob[:8] == b"\x89PNG\r\n\x1a\n"
    pos, width, height, data = 8, 0, 0, b""
    while pos < len(blob):
        (length,) = struct.unpack(">I", blob[pos:pos + 4])
        kind, body = blob[pos + 4:pos + 8], blob[pos + 8:pos + 8 + length]
        (crc,) = struct.unpack(">I", blob[pos + 8 + length:pos + 12 + length])
        assert crc == zlib.crc32(kind + body) & 0xFFFFFFFF
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            assert (depth, color) == (8, 2)
        elif kind == b"IDAT":
            data += body
        pos += 12 + length
    raw = zlib.decompress(data)
    rows = [np.frombuffer(raw[i * (width * 3 + 1) + 1:(i + 1) * (width * 3 + 1)], dtype=np.uint8) for i in range(height)]
    return np.stack(rows).reshape(height, width, 3)


def test_png_round_trips(tmp_path):
    image = np.random.default_rng(1).integers(0, 255, (5, 7, 3), dtype=np.uint8)
    write_png(tmp_path / "a" / "x.png", image)
    assert (decode_png((tmp_path / "a" / "x.png").read_bytes()) == image).all()


def test_png_needs_an_rgb_array(tmp_path):
    with pytest.raises(ValueError, match="height, width, 3"):
        write_png(tmp_path / "x.png", np.zeros((4, 4), dtype=np.uint8))


def test_hillshade_is_light_on_slopes_that_face_the_light():
    rows, cols = np.mgrid[0:20, 0:20]
    high_in_the_north_west = (-cols + rows).astype(np.float64) * 10.0  # it falls away toward the south-east
    high_in_the_south_east = -high_in_the_north_west                   # it falls away toward the north-west
    flat = np.zeros((20, 20))
    # The sun is in the north-west: a slope that faces it (high in the south-east) is lit, the other is dark.
    lit = hillshade(high_in_the_south_east, 30.0)[10, 10]
    dark = hillshade(high_in_the_north_west, 30.0)[10, 10]
    level = hillshade(flat, 30.0)[10, 10]
    assert dark < level < lit
    assert level == pytest.approx(np.cos(np.radians(45.0)))   # a flat plane under a 45 degree sun


def test_the_shaded_image_is_blue_at_sea_and_north_is_up():
    dem = np.zeros((10, 10))
    land = np.zeros((10, 10), dtype=bool)
    land[5:, :] = True  # rows 5.. are the NORTH half (row 0 is south)
    image = shaded_image(dem, land, 100.0, min_width=10)
    assert image.shape == (10, 10, 3)
    top, bottom = image[0, 5], image[9, 5]
    assert top[2] < top[1] + 80 and top[0] != bottom[0]  # land (top = north) vs sea (bottom = south)
    assert bottom[2] > bottom[0]                           # the sea is bluer than red


def test_small_images_are_enlarged_and_big_ones_thinned():
    dem = np.zeros((10, 20))
    land = np.ones((10, 20), dtype=bool)
    assert shaded_image(dem, land, 100.0, min_width=100).shape[1] == 100
    assert shaded_image(np.zeros((10, 2000)), np.ones((10, 2000), dtype=bool), 100.0, max_width=1000).shape[1] == 1000
