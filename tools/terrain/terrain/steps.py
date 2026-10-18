"""The build steps of ``python -m terrain.build_map`` and their registry.

A step is a function ``run(context)``. They run in this order (``STEP_ORDER``); ``--steps`` picks some of them.
Today ``fetch`` and ``corrections`` do real work; the others are stubs that say which Task fills them in, so the command
line, the step selection and the offline check are all in place and tested before the data work starts.
"""

from __future__ import annotations

import json
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

from .cache import CacheError, DownloadCache, Source
from .corrections import CorrectionError, MapState, apply_corrections, load_correction_files, write_report
from .projection import ProjectionError, make_to_world


class StepError(RuntimeError):
    """A step failed; the message says why."""


@dataclass
class Context:
    """What every step gets: the map's ``map.json``, where the package goes, the download cache."""

    map_file: Path
    map_def: dict
    out_dir: Path
    cache: DownloadCache
    offline: bool = False
    log: Callable[[str], None] = print
    # Files steps leave for each other (name -> path), e.g. the mosaic for the reprojection step.
    products: dict[str, Path] = field(default_factory=dict)
    # The working data (ground, land mask, rivers) once the steps that build it have run; None before.
    state: MapState | None = None

    def sources(self) -> list[Source]:
        entries = self.map_def.get("sources", [])
        if not isinstance(entries, list):
            raise StepError(f"{self.map_file}: 'sources' must be a list")
        try:
            return [Source.from_json(entry) for entry in entries]
        except CacheError as error:
            raise StepError(f"{self.map_file}: {error}") from None


@dataclass(frozen=True)
class Step:
    name: str
    run: Callable[[Context], None]
    description: str
    needs_downloads: bool = False  # true if it may download, so --offline checks the cache first


def load_map_def(map_file: Path) -> dict:
    """Reads ``map.json``: ``map_id`` and ``package`` are required (more keys arrive with the steps)."""
    try:
        map_def = json.loads(Path(map_file).read_text(encoding="utf-8"))
    except OSError as error:
        raise StepError(f"{map_file}: cannot read the map definition: {error.strerror or error}") from None
    except ValueError as error:
        raise StepError(f"{map_file}: not valid JSON: {error}") from None
    if not isinstance(map_def, dict):
        raise StepError(f"{map_file}: the map definition must be a JSON object")
    for key in ("map_id", "package"):
        if not isinstance(map_def.get(key), str) or not map_def[key]:
            raise StepError(f"{map_file}: missing required text field '{key}'")
    return map_def


def _fetch(context: Context) -> None:
    sources = context.sources()
    if not sources:
        context.log("fetch: the map lists no sources")
        return
    for source in sources:
        cached = context.cache.is_valid(source)
        path = context.cache.fetch(source)
        context.log(f"fetch: {source.dataset}/{source.name} {'(cached)' if cached else '(downloaded)'} -> {path}")


def _corrections(context: Context) -> None:
    """Reads the overlay files listed in map.json ("corrections": [paths relative to map.json]) and checks them.
    If the working data exists, applies them in order and writes a report beside the package; if not (the steps
    that build it are T1.2.2.1), it only validates the files."""
    listed = context.map_def.get("corrections", [])
    if not isinstance(listed, list) or not all(isinstance(p, str) for p in listed):
        raise StepError(f"{context.map_file}: 'corrections' must be a list of file paths")
    if not listed:
        context.log("corrections: map.json lists none; nothing to do")
        return
    try:
        to_world = make_to_world(context.map_def)
        corrections = load_correction_files([context.map_file.parent / p for p in listed], to_world)
        if context.state is None:
            context.log(f"corrections: {len(corrections)} correction(s) in {len(listed)} file(s) read and checked; "
                        "the ground is not built yet (T1.2.2.1), so none was applied")
            return
        effects = apply_corrections(context.state, corrections, to_world)
    except (CorrectionError, ProjectionError) as error:
        raise StepError(str(error)) from None
    report = context.out_dir.parent / f"{context.out_dir.name}.reports" / "corrections.md"
    write_report(report, effects, f"Historical corrections: {context.map_def['map_id']}")
    context.log(f"corrections: applied {len(effects)} correction(s); report: {report}")


def _stub(name: str, task: str, what: str) -> Callable[[Context], None]:
    def run(context: Context) -> None:
        context.log(f"{name}: not implemented yet ({task}: {what}); nothing done")

    return run


STEPS: tuple[Step, ...] = (
    Step("fetch", _fetch, "download the source datasets into the cache", needs_downloads=True),
    Step("mosaic", _stub("mosaic", "T1.2.2.1", "join the DEM tiles"), "join the source DEM tiles"),
    Step("reproject", _stub("reproject", "T1.2.2.1", "reproject to the map projection"),
         "reproject to the map's equal-area projection"),
    Step("corrections", _corrections, "apply the historical correction overlays (architecture §15.7)"),
    Step("region", _stub("region", "T1.2.2.1", "write the region rasters"), "write the 1 km region rasters"),
    Step("detail", _stub("detail", "T1.2.2.1", "write the 32 m detail tiles"), "write the 32 m detail tiles"),
    Step("rivers", _stub("rivers", "T1.2.3.1", "extract rivers and lakes"), "extract rivers and lakes"),
    Step("layers", _stub("layers", "T1.2.4.1", "derive fertility, vegetation, wetness, deposits"),
         "derive fertility, vegetation, wetness and deposit layers"),
    Step("package", _stub("package", "T1.2.6.1", "write package.json with checksums"),
         "write package.json with the checksums and the package hash"),
)
STEP_ORDER: tuple[str, ...] = tuple(step.name for step in STEPS)
_BY_NAME = {step.name: step for step in STEPS}


def select_steps(names: list[str] | None) -> list[Step]:
    """The steps to run, in pipeline order. ``None`` means all. Raises StepError for an unknown name."""
    if names is None:
        return list(STEPS)
    unknown = [name for name in names if name not in _BY_NAME]
    if unknown:
        raise StepError(f"unknown step(s): {', '.join(unknown)}. The steps are: {', '.join(STEP_ORDER)}")
    wanted = set(names)
    return [step for step in STEPS if step.name in wanted]
