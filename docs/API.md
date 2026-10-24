# Local C++ API

The native executable serves the API and `frontend/dist` on **127.0.0.1**, port 8000 by default. Start with `build/distriarch.exe serve --root . --port 8000` or `launch.ps1`. Requests and responses use JSON. No external account or service is involved.

| Method | Route | Behavior |
|---|---|---|
| GET | `/api/health` | Status, model, `implementation: C++20`, version 3.0.0 |
| GET | `/api/scenarios` | Five scenario IDs, names and descriptions |
| GET | `/api/config/{name}?seed=11` | Full normalized editable input; unknown name 404, invalid unsigned 32-bit seed 422 |
| POST | `/api/validate` | Strictly validate and return normalized configuration |
| POST | `/api/run` | Execute one native simulation and return complete config, summary and audit trace |
| POST | `/api/compare` | Run static, demand and cost on copies of identical input; return summaries and workload/hardware hashes |
| GET | `/api/experiments` | Saved `results/summary.json`, or an empty catalog if no experiments exist |
| GET | `/` | Built React dashboard; JavaScript/CSS assets served by the same native HTTP server |

POST bodies are the configuration itself, not wrapped. Obtain a complete starting body from `/api/config/phases`. Example with PowerShell:

```powershell
$config = Invoke-RestMethod http://127.0.0.1:8000/api/config/phases
$body = $config | ConvertTo-Json -Depth 20
$result = Invoke-RestMethod http://127.0.0.1:8000/api/run -Method Post -ContentType application/json -Body $body
$result.summary
```

Configuration keys: name, description, seed, policy, resources, links, workloads, settings, horizon and max_events. Resource and phase fields/units are documented in README and represented by C++ structs in `include/distriarch/model.hpp`. `configs/*.json` are complete runnable examples. The parser rejects unknown fields, nonfinite/negative demands, duplicate IDs, disconnected resources, invalid policies and out-of-range counts. Bounds: 24 resources, 60 links, 16 workloads, 12 phases per workload, 100 chunks per phase, 500,000 events, 10,000 simulated seconds. Defaults are smaller.

Successful run responses have `version: 3`, `trace_schema: event-boundaries-v1`, `implementation: C++20`, `config`, `status`, `elapsed`, `processed_events`, `workload_hash`, `hardware_hash`, `events`, `intervals`, `transfers`, `ownership`, `compositions`, `transitions`, `discovery`, `discovery_events`, `jobs` and `summary`. Incomplete architectural runs are valid 200 responses with explicit status/unfinished metrics; they do not become fabricated completions. Invalid input returns 422 with a `detail` message. At most two simulations/comparisons execute at once; requests arriving while both slots are occupied return 429. Payload limit is 4 MiB. The HTTP pool has four workers. HTTP parser errors use their appropriate error status.

Fingerprints use SHA-256 over sorted compact JSON with the native serializer, truncated to 16 hex characters. Identical typed inputs produce identical fingerprints across native policies and API/CLI. Older serializers can print floating values differently, so historical hashes are not a migration-equivalence claim; the historical v2 81-run migration parity is not a claim about the corrected behavior.

The API intentionally has no `/docs` dependency. This document is its local contract. `node tests/http_smoke.mjs` starts a temporary native server on port 8001, checks 25 real HTTP/CLI cases, and stops only that process.

## Event-prefix contract (v3)

Cursor k is inclusive. Events are stably ordered by index; a timestamp never substitutes for this order. Ownership, compositions and phase history have event_index. discovery_events maps each resource to its discovery event. Transitions/transfers have start_event and nullable end_event. Service/link intervals have requested_event and nullable end_event. New send/queue/hop events explicitly publish submitted flows, reservations in a queue and hop completions. An unknown end_event remains unfinished at timeout/event budget. Ownership rows sharing a commit/release index publish atomically.

frontend/src/replay.ts filters all record collections by these indices, projects pending end times as null and marks interval completion by event order. Time is used only for service progress/queueing within already-published records. Final metrics and exports use the complete original trace. A historical v2 trace cannot serve as a v3 event-step assertion.
