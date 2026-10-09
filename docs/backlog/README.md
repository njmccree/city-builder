# Backlog

The backlog has four levels, Agile-style:

| Level | Meaning | Example | ID |
|---|---|---|---|
| **Epic** | A large area of the game, delivered across one or more milestones | E0 Foundation & Walking Skeleton | `E0` |
| **Feature** | A coherent capability inside an Epic | Time & Calendar | `F0.3` |
| **User Story** | A user-visible (or developer-visible) outcome with acceptance criteria. **One branch and one PR per Story.** | "As a player, I can pause and change speed" | `S0.3.1` |
| **Task** | **One prompt to Claude** (sometimes 2–3 follow-ups). One commit. | Implement `GameClock` | `T0.3.1.1` |

## Task format

Every Task lists:
- **Model:**
  - `Sonnet` is the default.
  - `Opus` is for architecture-critical or algorithmically hard work. Use it sparingly: it consumes Pro-plan quota quickly.
  - `Haiku` is for pure data or content entry.
- **Session:**
  - `[Cloud]` needs only build and test, so it can run in a claude.ai cloud session.
  - `[PC]` needs Windows, a GPU, or the Godot editor, so run it in Claude Code on your PC.
- **Depends on:** Tasks that must be merged first.
- **Prompt:** paste it as-is into a new Claude Code session at the repo root. Prompts assume `CLAUDE.md` and `docs/architecture.md` exist; Claude Code loads `CLAUDE.md` automatically.

### How to run a Task
1. Start a new session: a cloud session for `[Cloud]` Tasks, local Claude Code for `[PC]` Tasks. Choose the model the Task names.
2. Paste the prompt.
3. Review the result. Check its claims against the acceptance criteria. For `[PC]` Tasks, do the manual check the Task describes.
4. When a Story's last Task is done, review and merge its PR (squash).
5. If a Task fails twice on Sonnet, retry it on Opus. Note that in the Task's entry so later Tasks can be sized better.

## Status legend
`[ ]` not started · `[~]` in progress · `[x]` done (ticked by the Task's own commit)

## Epic roadmap (tentative — refined as design rounds finish)

| Epic | Title | Milestone | Status |
|---|---|---|---|
| **E0** | **Foundation & Walking Skeleton** → [E0-foundation.md](E0-foundation.md) | M0 | **Drafted** |
| E1 | Terrain & World: real-data pipeline, region map, city terrain, water | M1 | Awaiting design rounds |
| E2 | City Building: roads/paths, zoning, ploppables, construction, logistics | M1 | Awaiting design rounds |
| E3 | Citizens: agents, needs, life cycle, traits, households, simulation LOD | M1 | Awaiting design rounds |
| E4 | Economy, Industry & Trade | M1–M2 | Round 4 paused |
| E5 | Government, Offices, Ranks & Politics | M2 | Awaiting Round 5 |
| E6 | Realms, Diplomacy, NPC AI & Warfare | M3 | Awaiting Round 5 |
| E7 | Culture, Religion, Society & Emergent Neighborhoods | M2 | Partly designed |
| E8 | Progression: Tech/Culture Trees, Eras, Events | M3 | Awaiting Round 8 |
| E9 | UI/UX, Overlays & Encyclopedia | M1+ | Awaiting Round 9 |
| E10 | Art & Audio Pipeline, Custom Building | M1+ | Awaiting Round 9 |
| E11 | Modding, Steam & Release Engineering | M4 | Partly designed |
| E12 | Launch Content: maps, cultures, bookmarks, history | M4 | Awaiting Round 11 |

**Milestones (draft):**
- **M0:** walking skeleton (E0).
- **M1:** one playable city in one era (sandbox).
- **M2:** government and society.
- **M3:** multiple cities, realms and eras.
- **M4:** launch content and release.
