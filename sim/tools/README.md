# sim/tools

Command-line tools built on the core: `headless/` (citysim_headless) and `validate/` (citysim_validate).

## citysim_headless

Runs the simulation without a front end (currently the demo scenario in `headless/demo_scenario.*`, until the base game has real systems).

```
citysim_headless --seed 42 --ticks 500                  # prints "tick hash" every 100 ticks, then "final <tick> <hash>"
citysim_headless --seed 42 --ticks 500 --record-log run.txt
citysim_headless --replay run.txt                       # same final hash as the recording
```

The log is a text format (documented in `headless/input_log_text.hpp`); S0.5 replaces it with the binary container. `--hash-every 0` prints only the final hash.
