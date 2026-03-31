# Benchmarks: Blaze vs Python

This folder contains simple, repeatable benchmark pairs:

- `arithmetic_loop` - integer arithmetic in a tight loop
- `hashmap_set_workload` - insert/get map operations + set inserts
- `hashmap_string_and_class_keys` - hashmap lookups with string and object/class keys
- `string_join_workload` - build list/array + join into a string

Each case has two files with equivalent logic:

- `*.blaze` for Blaze
- `*.py` for Python

## Run

Build Blaze first:

```bash
cmake --build build
```

Then run:

```bash
python3 bench/run_benchmarks.py
```

The runner prints median runtime over multiple runs and checks output parity.
