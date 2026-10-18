# game/content/mods

Bundled and development mods. The base game itself is the `base` mod in `game/content/base`; a mod is a folder
shaped exactly like it.

## Structure of a mod

```
my_mod/
  mod.json            required: the manifest
  defs/**/*.json      definitions: {"defs": [ {"type": "...", "id": "my_mod:...", ...} ]}
  defines/time.json   tunable constants (only the fields you set change)
  loc/<lang>/*.json   text tables: flat {"key": "text"}
```

`mod.json`:

```json
{
  "id": "my_mod",
  "name_key": "mod.my_mod.name",
  "version": "1.0.0",
  "depends_on": ["base"],
  "load_after": [],
  "enabled_by_default": true
}
```

- `id`: lowercase letters, digits, `_` and `.`. It is the part before the `:` in the ids of what the mod adds.
- `version`: a semantic version (`MAJOR.MINOR.PATCH`, optionally `-prerelease` and `+build`).
- `depends_on`: mods that must be enabled and load first. A missing one is an error.
- `load_after`: optional ordering. If the named mod is enabled this mod loads after it; if not, it is ignored.
- `enabled_by_default`: `false` for a mod that ships in the repository but must be switched on.

## Load order

Mods load in dependency order, with `base` always first. Among mods that could go next, the smallest `id` goes first,
so the order is the same on every machine. A dependency cycle is an error that names the cycle.

## Adding, replacing and patching

- **Add** a definition with a new id (use your own prefix: `my_mod:era.extra`).
- **Replace** one by writing a full definition with the id of an existing one from an earlier mod. The new one
  wins, whole. Two definitions with the same id inside one mod are a mistake and an error.
- **Patch** one by adding `"patch": true` and only the fields you want to change, together with `type` and `id`.
  This is a JSON Merge Patch (RFC 7386): objects merge, a `null` removes a field, and anything else, including arrays,
  replaces the old value. Patching an id that does not exist is an error.

```json
{ "defs": [ { "type": "era", "id": "base:era.ancient", "patch": true, "desc_key": "my_mod.ancient.desc" } ] }
```

## Checking your mod

```
build/<preset>/sim/tools/validate/citysim_validate game/content --strict
```

reports every problem as `file:json_pointer: message`. `example_mod` here is a complete, working example: it patches
the description of the Ancient era. It is disabled by default.
