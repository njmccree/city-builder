import pytest

from terrain.projection import ProjectionError, make_to_world, projection_center, world_size

MAP = {"bounds": {"min_lon": 24.0, "min_lat": 21.0, "max_lon": 50.0, "max_lat": 38.5},
       "projection": {"type": "laea", "lat_0": 30.0, "lon_0": 38.0}}


def test_the_world_frame_starts_at_the_south_west_corner_of_the_projected_bounds():
    to_world = make_to_world(MAP)
    width, height = world_size(MAP)
    assert 2_000_000 < width < 3_500_000 and 1_500_000 < height < 2_500_000
    for lon in (24.0, 37.0, 50.0):
        for lat in (21.0, 30.0, 38.5):
            x, z = to_world(lon, lat)
            assert -1.0 <= x <= width + 1.0 and -1.0 <= z <= height + 1.0
    # east and north are increasing
    assert to_world(40.0, 30.0)[0] > to_world(30.0, 30.0)[0]
    assert to_world(35.0, 35.0)[1] > to_world(35.0, 25.0)[1]


def test_the_center_is_the_projection_origin():
    center = make_to_world(MAP)(38.0, 30.0)
    center_default = make_to_world({"bounds": MAP["bounds"]})(37.0, 29.75)
    assert projection_center(MAP) == (30.0, 38.0)
    assert projection_center({"bounds": MAP["bounds"]}) == (29.75, 37.0)
    assert center != center_default


def test_bad_bounds_are_refused():
    with pytest.raises(ProjectionError, match="needs 'bounds'"):
        make_to_world({})
    with pytest.raises(ProjectionError, match="min < max"):
        make_to_world({"bounds": {"min_lon": 10, "min_lat": 0, "max_lon": 5, "max_lat": 1}})
