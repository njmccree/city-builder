# Spike: movement scale and day length

**Status: approved by the designer on 2026-10-18: option C.** `movement_scale` 6 and `day_length_ticks` 28,800 are now
the defaults (`game/content/base/defines/time.json` and `TimeDefines`). The measurements and the reasoning below are
unchanged; the recommendation section is what was approved. The mitigations (options 1 to 3: fast-forwarding unobserved
trips, coarse schedule blocks, faster vehicles) are still to be built; the open questions at the end are not decided yet.

Story S0.8.2, Task T0.8.2.1. Related: `docs/architecture.md` §6 (the time model and its "known design risk"),
`docs/gdd.md` §3.3 (time cadence) and §10.11 (daily life).

## The problem

Two settings decide how a day feels:

- **`movement_scale`**: simulated seconds of walking per tick is `movement_scale / base_tick_rate`. At 20 ticks per
  second and speed 1x, an agent that walks 1.4 m/s "in the world" crosses `1.4 x movement_scale` meters of screen per
  real second. So it is also how fast people look.
- **`day_length_ticks`**: how many ticks one in-game day lasts, at 20 ticks per second and 1x. So
  `day_length_ticks / 480` real seconds is one in-game hour.

People walk at real-world speed on a real-world map, but a day is squeezed into minutes. A real 1 km walk takes
about 12 minutes (0.2 h). In a 12-minute game day, one in-game hour lasts 30 real seconds, so to cover 1 km in
0.2 h an agent would have to cross it in 6 real seconds: 167 m/s on screen, which no player would accept. Show
people at a believable speed and their trips take far too many in-game hours; make the trips fit a schedule and
people look like they are flying. This spike measures the trade-off so the designer can pick.

The calendar is a separate clock (an Ancient year is 120 real seconds at 1x) and is not affected by anything here.

## What was measured, and how

`game/tests/spikes/measure_time_scale.gd` runs the real game windowed (Windows, GTX 1660 Ti, Release build of the
extension, commit `f35a802` plus the new `set_time_scale` command), starts a new game on a 128 x 128 cell test map,
spawns 1,500 wandering agents and, for each configuration, sets it with the console command
`time_scale <movement_scale> <day_length_ticks>`. Then at speed 1x it measures:

- **walk m/s**: the mean straight-line distance the agents cover per real second, from snapshot positions;
- **screen px/s**: how far an agent moves across a 1,920 x 1,061 screen per second, with the camera 60 m above the
  middle of the map (positions projected through the camera);
- **clock**: how fast the in-game clock face (HH:MM) advances, which gives the real length of a day;
- **h/km**: in-game hours to walk 1 km at that pace, from the first and third numbers.

Each window was checked to run at a steady 20 ticks per second. (An earlier run measured two settings while the
simulation was catching up after a stall, at about 58 ticks per second, and gave nonsense; the script now waits for
20 +/- 0.5 ticks per second before it samples. Worth knowing: a stall makes the game briefly run faster than real
time.)

The agents' own speeds are 1.0 to 1.8 m/s, 1.4 m/s on average, which is why the walking speed at scale 1 is 1.39.

### `set_time_scale` command

To change the two values at run time without breaking determinism, this Task adds the command `SetTimeScale
{movement_scale, day_length_ticks}` (console: `time_scale <scale> <day ticks>`). It is a normal recorded command
that changes the hashed `TimeDefines`, so replays and saves stay exact (tests cover its effect, bad values, the state
hash and the input-log round trip). A value outside 0 to 100 for the scale, or a day under 24 ticks, is ignored with
a warning.

## Configurations and results

All at speed 1x (20 ticks per second). "Real s per game hour" is `day_length_ticks / 480`. The last four columns
come from the measured walking pace.

| Config | `movement_scale` | `day_length_ticks` | Day in real minutes | Real s per game hour | Walk m/s (measured) | Screen px/s at 60 m (measured) | h per 1 km (measured) | h per 3 km | One-way reach in 1 h |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A | 1 | 7,200 | 6 | 15 | 1.39 | 5 | 47.8 | 143 | 21 m |
| B (current default) | 3 | 14,400 | 12 | 30 | 4.15 | 17 | 8.0 | 24 | 125 m |
| C | 6 | 28,800 | 24 | 60 | 8.16 | 38 | 2.0 | 6.1 | 490 m |
| D | 6 | 14,400 | 12 | 30 | 8.13 | 39 | 4.1 | 12.3 | 244 m |
| E | 12 | 14,400 | 12 | 30 | 15.45 | 60 | 2.2 | 6.5 | 463 m |

Measured clock rates matched the settings exactly (4.00, 2.00, 1.00, 2.00 and 2.00 game minutes per real second).
At scale 12 the measured walking speed (15.5) is below the 16.8 m/s of the formula, and slightly below at scale 6,
because agents turn and reach their goals within a sample window and the measure is straight-line distance. Screen
speeds are indicative: they depend on the camera height and on where an agent is on screen.

### How a citizen's day would fit

A simple day from the brief: 8 h sleep, 8 h work, 2 h errands, 6 h free. Sleep and work are at home and at work, so
travel competes with the other **8 hours** (errands plus free time). Commuting 1 km each way to work and back is two
one-way trips; the share below is one 1 km round trip divided by those 8 hours.

| Config | One 1 km round trip | Share of the 8 free hours | Fits? |
|---|---:|---:|---|
| A | 96 h | 1,195% | no |
| B (default) | 16 h | 201% | no: the commute alone is longer than the day |
| C | 4.1 h | 51% | tight, but a day still works |
| D | 8.2 h | 102% | no |
| E | 4.3 h | 54% | tight, but it looks like sprinting |

## Findings

1. **Plain walking cannot fit a realistic schedule at any believable speed.** Even config C, with a 24-minute day
   and agents at 8 m/s, spends half of the free time on a 1 km round trip. The current default (B) makes a 1 km
   commute last a full day. Whatever the numbers, long trips must be solved some other way (see the options).
2. **A faster speed looks better than it sounds.** From the usual city-builder camera heights the ground speed is
   hard to feel: at 60 m above the ground, scale 6 moves an agent only 38 px per second (2% of the screen width),
   and from higher up much less. The ground speed is mainly noticeable when the camera is at street level, where
   8 m/s looks like running and 15 m/s like sprinting.
3. **Day length helps as much as speed, and costs a different thing.** Doubling the day from 12 to 24 real minutes
   halves the hours per kilometer (D to C) without making anyone look faster, but the day/night cycle becomes slow
   (still 6 minutes at 4x) and fewer schedule blocks pass per real minute. It does not touch the calendar.
4. **The two settings are independent of the calendar and of the simulation cost.** Neither changes tick work, so
   either can be tuned late. Both are saved and hashed state, so a change takes effect through the command and is
   in every save and replay.

## Options for trips that do not fit

These can be combined; none is implemented yet.

1. **Fast-forward unobserved trips.** An agent that is off screen (or in a far district) is not moved step by step: its
   arrival time is computed from distance and a real-world travel speed, and it is placed at the destination then.
   Observed agents still walk visibly, at the scaled speed, and are not held to the schedule down to the minute.
   Cheapest at scale and matches the design note in `docs/gdd.md` §3.3 ("citizens skip days at a statistical level").
2. **Coarse schedule blocks.** Plan the day as sleep / work / errands / leisure blocks with slack, not as exact
   times; a trip that takes longer than the slack shortens the next block instead of breaking the plan.
3. **Faster travel.** Carts, horses, trams and cars cover distance at 3 to 10 times walking speed, so a vehicle
   trip is short in game hours while still looking sensible. This also gives the eras something to progress with.
4. **City layout.** At config C the one-hour walking reach is about 490 m. Mixed-use districts where most daily needs
   are within a few hundred meters make walking schedules work; it is also what a pre-industrial city looked like.
5. **Visual speed-up only while watched.** Raise the speed of agents near the camera (a "catch-up" walk) so what
   the player sees is quick while the schedule stays honest. Risk: agents appear to teleport or hurry; test before
   choosing it.

## Recommendation (for designer approval)

**Set the default to config C: `movement_scale` 6 and `day_length_ticks` 28,800** (a 24-minute day at 1x, 6 minutes
at 4x, one game hour per real minute; agents at about 8 m/s). Reasons:

- It brings a 1 km trip down to about two game hours at a running pace (E needs sprint-like 15 m/s to get the same
  hours per kilometer, and A, B and D are far above it).
- On screen it is calm: 38 px per second from 60 m up, much less from the normal camera heights.
- The day is long enough that the lighting cycle and the day's blocks are readable, and still short enough at 4x.

Pair it with options 1 and 2 (fast-forward unobserved trips; coarse schedule blocks) and option 3 (vehicles at 3x
or more) from the start, because even C leaves a 3 km trip at over 6 game hours. Treat 500 m as the design reach of
a one-hour walk when laying out districts.

Open questions for the designer:

- Is a 24-minute day acceptable, or should it stay at 12 minutes (config D, with its 4 hour kilometer)?
- Should observed agents be allowed to hurry (option 5), or must a watched agent always walk at the shown speed?
- Should speed and day length be per era (for example slower, wider days in the Ancient era)?

## Not covered

No citizen schedule was simulated: the walking skeleton only wanders, so the schedule fit is arithmetic on the
measured walking pace. Fast-forwarding, vehicles and observed-agent catch-up are not built. Screen speeds were
measured at one camera height and one resolution. The measurements use a single seed and map.
