# nav — Tier-2 routing scenarios

Each `*.json` file holds one or more **flat JSON objects, one per line** (shared
`DcDecisionJson` format; `#` lines and blanks are ignored). Each object is a
frozen routing problem replayed against a real sliced navmesh by
`t/TestNavGeometry.cpp`. The suite `GTEST_SKIP`s unless the matching map slice
exists under `../mapdata/mmaps/` (see `tools/slice_mapdata.py`).

## Schema

| key               | type  | required | meaning                                            |
|-------------------|-------|----------|----------------------------------------------------|
| `name`            | str   | no       | label for assertion messages                       |
| `map`             | uint  | yes      | map id (e.g. 189 = Scarlet Monastery)              |
| `sx`,`sy`,`sz`    | float | yes      | start world coords                                 |
| `tx`,`ty`,`tz`    | float | yes      | goal world coords                                  |
| `expectReachable` | bool  | no (true)| whether a route should exist                       |
| `expectComplete`  | bool  | no       | if set, assert the route reaches the goal poly     |
| `maxStepZ`        | float | no       | if set, assert no vertical pop between points > it (catches under-map / ledge seams) |
| `minPoints`       | uint  | no       | if set, assert the polyline has at least this many points |

## Example line

```
{"name":"cathedral entry to nave","map":189,"sx":1690.1,"sy":1053.4,"sz":18.6,"tx":1660.0,"ty":1110.0,"tz":18.6,"expectReachable":true,"expectComplete":true,"maxStepZ":3.0}
```

## Authoring from a real bug

1. Slice the map: `tools/slice_mapdata.py --datadir <data> --map <id>`.
2. Get the start/goal coords from the live repro (the DungeonClear log prints
   them, or `.gps` in-game at each spot).
3. Add a line with the expected outcome. A below-ledge target that must NOT be
   routable is `"expectReachable":false`; an under-map seam is caught with a
   tight `"maxStepZ"`.
