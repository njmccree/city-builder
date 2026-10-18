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
