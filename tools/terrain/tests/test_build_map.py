import hashlib
import json

import pytest

from terrain import build_map
from terrain.steps import STEP_ORDER, StepError, select_steps


def write_map(tmp_path, **extra):
    map_file = tmp_path / "map.json"
    map_file.write_text(json.dumps({"map_id": "test:map.x", "package": "x", **extra}))
    return map_file


def run(tmp_path, *args):
    return build_map.main([*args, "--out", str(tmp_path / "out"), "--cache", str(tmp_path / "cache")])


def test_steps_are_in_pipeline_order():
    assert STEP_ORDER == ("fetch", "mosaic", "reproject", "corrections", "region", "detail", "rivers", "layers",
                          "package")
    assert [s.name for s in select_steps(["package", "fetch"])] == ["fetch", "package"]
    with pytest.raises(StepError, match="unknown step.*nope"):
        select_steps(["fetch", "nope"])


def test_all_stub_steps_run_and_say_which_task_fills_them(tmp_path, capsys):
    assert run(tmp_path, str(write_map(tmp_path))) == 0
    out = capsys.readouterr().out
    assert "building test:map.x" in out
    assert "not implemented yet (T1.2.2.1" in out
    assert out.strip().endswith("done")
    assert (tmp_path / "out").is_dir()


def test_steps_option_selects_steps(tmp_path, capsys):
    assert run(tmp_path, str(write_map(tmp_path)), "--steps", "rivers,layers") == 0
    out = capsys.readouterr().out
    assert "== rivers" in out and "== layers" in out and "== mosaic" not in out


def test_usage_errors_exit_2(tmp_path, capsys):
    assert run(tmp_path, str(write_map(tmp_path)), "--steps", "bogus") == 2
    assert "unknown step" in capsys.readouterr().err
    assert run(tmp_path, str(tmp_path / "missing.json")) == 2
    (tmp_path / "bad.json").write_text("{not json")
    assert run(tmp_path, str(tmp_path / "bad.json")) == 2
    (tmp_path / "nofields.json").write_text("{}")
    assert run(tmp_path, str(tmp_path / "nofields.json")) == 2
    assert "missing required text field 'map_id'" in capsys.readouterr().err


def test_fetch_downloads_into_the_cache(tmp_path, server, capsys):
    data = b"elevation" * 100
    server.files["/tile.tif"] = data
    map_file = write_map(tmp_path, sources=[{
        "dataset": "copdem", "name": "tile.tif", "url": server.base_url + "/tile.tif", "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest()}])
    assert run(tmp_path, str(map_file), "--steps", "fetch") == 0
    assert (tmp_path / "cache" / "copdem" / "tile.tif").read_bytes() == data
    assert "(downloaded)" in capsys.readouterr().out
    assert run(tmp_path, str(map_file), "--steps", "fetch") == 0
    assert "(cached)" in capsys.readouterr().out
    assert len(server.requests) == 1


def test_offline_fails_fast_when_a_file_is_not_cached(tmp_path, server, capsys):
    server.files["/tile.tif"] = b"x"
    map_file = write_map(tmp_path, sources=[{"dataset": "copdem", "name": "tile.tif",
                                             "url": server.base_url + "/tile.tif"}])
    assert run(tmp_path, str(map_file), "--offline") == 1
    err = capsys.readouterr().err
    assert "--offline" in err and "copdem/tile.tif" in err
    assert server.requests == []  # nothing was asked of the network
    assert not (tmp_path / "out").exists()  # and no work was started
    # Offline is fine for steps that need no downloads.
    assert run(tmp_path, str(map_file), "--offline", "--steps", "rivers") == 0


def test_a_network_failure_is_a_clean_error(tmp_path, capsys):
    map_file = write_map(tmp_path, sources=[{"dataset": "d", "name": "x", "url": "http://127.0.0.1:1/x"}])
    assert run(tmp_path, str(map_file), "--steps", "fetch") == 1
    assert "HTTPS_PROXY" in capsys.readouterr().err


# --- the corrections step ------------------------------------------------------------------------------------------

BOUNDS = {"min_lon": 24.0, "min_lat": 21.0, "max_lon": 50.0, "max_lat": 38.5}


def write_overlay(path, *features):
    path.write_text(json.dumps({"type": "FeatureCollection", "features": list(features)}))


def overlay_feature(op="set_water", **properties):
    ring = [[30.0, 29.0], [31.0, 29.0], [31.0, 30.0], [30.0, 30.0], [30.0, 29.0]]
    props = {"op": op, "source": "a test", "confidence": "medium", "level_m": 0, **properties}
    return {"type": "Feature", "geometry": {"type": "Polygon", "coordinates": [ring]}, "properties": props}


def test_the_corrections_step_checks_the_listed_files(tmp_path, capsys):
    write_overlay(tmp_path / "a.geojson", overlay_feature(name="Gulf"))
    map_file = write_map(tmp_path, bounds=BOUNDS, corrections=["a.geojson"])
    assert run(tmp_path, str(map_file), "--steps", "corrections") == 0
    assert "1 correction(s) in 1 file(s) read and checked" in capsys.readouterr().out

    write_overlay(tmp_path / "a.geojson", overlay_feature(source=""))
    assert run(tmp_path, str(map_file), "--steps", "corrections") == 1
    assert "missing property 'source'" in capsys.readouterr().err

    map_file = write_map(tmp_path, bounds=BOUNDS, corrections=["missing.geojson"])
    assert run(tmp_path, str(map_file), "--steps", "corrections") == 1
    assert "cannot read the file" in capsys.readouterr().err


def test_the_corrections_step_needs_bounds_and_lists_nothing_otherwise(tmp_path, capsys):
    write_overlay(tmp_path / "a.geojson", overlay_feature())
    assert run(tmp_path, str(write_map(tmp_path, corrections=["a.geojson"])), "--steps", "corrections") == 1
    assert "needs 'bounds'" in capsys.readouterr().err
    assert run(tmp_path, str(write_map(tmp_path)), "--steps", "corrections") == 0
    assert "lists none" in capsys.readouterr().out
    assert run(tmp_path, str(write_map(tmp_path, corrections="a.geojson")), "--steps", "corrections") == 1


def test_the_corrections_step_applies_them_when_the_ground_exists(tmp_path):
    import numpy as np

    from terrain.cache import DownloadCache
    from terrain.corrections import Grid, MapState
    from terrain.projection import make_to_world, world_size
    from terrain.steps import Context, load_map_def, select_steps

    write_overlay(tmp_path / "a.geojson", overlay_feature(name="Gulf", level_m=0))
    map_file = write_map(tmp_path, bounds=BOUNDS, corrections=["a.geojson"])
    map_def = load_map_def(map_file)
    width_m, height_m = world_size(map_def)
    cells = (int(height_m // 5000) + 1, int(width_m // 5000) + 1)
    state = MapState(Grid(np.full(cells, 50.0), 0.0, 0.0, 5000.0), Grid(np.ones(cells, dtype=bool), 0.0, 0.0, 5000.0))
    logs = []
    context = Context(map_file=map_file, map_def=map_def, out_dir=tmp_path / "out" / "pkg",
                      cache=DownloadCache(tmp_path / "cache"), log=logs.append, state=state)
    select_steps(["corrections"])[0].run(context)
    x, z = make_to_world(map_def)(30.5, 29.5)  # inside the polygon
    row, col = int(z // 5000), int(x // 5000)
    assert state.dem.data[row, col] == -2.0 and not state.land.data[row, col]
    assert state.dem.data[0, 0] == 50.0
    report = tmp_path / "out" / "pkg.reports" / "corrections.md"
    assert "## 1. set_water: Gulf" in report.read_text(encoding="utf-8")
    assert any("applied 1 correction(s)" in line for line in logs)
