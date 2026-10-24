# DistriArch v3 verification record

Verified 24 October 2026 (Asia/Calcutta). Project identity: **DistriArch — Dynamic Resource Composition Simulator**, **Option 2 — Distributed Computer Architecture**, **Gyan Chandra — CS23I1053**. The existing C++20 engine, native HTTP server and React/TypeScript dashboard are preserved.

Environment: Windows x64; GCC 16.2.0 / MSYS2 MinGW-w64; zlib 1.3.2; PowerShell 7.6.5; Node 24.22.1; pnpm 11.25.0; Vite 7.1.7 and TypeScript 5.9.3. Node is development/browser-test tooling, not an application runtime dependency.

The current source and the original three-page ProjectStatements.pdf were inspected before implementation edits. Page 1 was text-extracted and visually checked, including Option 2's example of reallocating unused resources. The PDF remains SHA-256 `6cb81a7ffe544dd195851c973d55778cc97a00b5f24fef0c581dc70ee9bc04c6`. The original submission ZIP remains unchanged with SHA-256 `6be6055df785155082b5d9a8230d8af88f510d1221619f1c7f2b1da98838a577`. See [original-file hashes](evidence/fixed-original-files.json).

## Pass 1 — independently reproduced defects and targeted regressions: PASS

The initial 15 native regression cases were compiled and executed against the unchanged old engine before the implementation fix: **10 failed, 5 passed**. The failures exposed memory/storage reclamation in both adaptive policies, unused cores/device retention, the two missing growth costs and missing authoritative event publication metadata. Static semantics, no-growth/shrink cost cases and the explicit idealization passed as expected. [Actual old output](evidence/fixed-regressions-before.txt).

The new browser policy and commit assertions were also executed against the old dashboard: **both failed**. cpu-a reverted 777 → 70 after a policy change (other custom link/settings values were also lost). Cursor 41 showed a composed system before cursor 42's same-time commit. [Old browser assertion output](evidence/fixed-browser-before.json).

Eight additional plan/corruption checks were then added, bringing the corrected targeted C++ group to **23 passed, 0 failed**. Command: `.\build\distriarch_tests.exe --regressions`. [Final targeted output](evidence/fixed-targeted-tests.txt).

Corrections and evidence:

1. **Reclaim unused capacity safely.** Adaptive policies first reclaim a declared smaller memory/storage range in place, remove an unused accelerator and reduce idle CPU holdings to one core. No future phases/arrivals are consulted. Growth remains subject to capacity feasibility. Reclamation bypasses the optional speedup/cooldown gate but executes commands/acks, preserves old holdings until commit, atomically publishes ownership changes and wakes waiters. Static semantics are preserved.
2. **Complete shared transition plans.** Estimation and execution use the same ordered move/preparation/control plan. Old-only endpoints also receive commands/acks. Retained-state migration and additional storage-to-memory loading each include source read, every hop and destination write. There is no omitted growth write and no omitted load after migration.
3. **Retain custom input on policy-only changes.** The policy updates the current config without a default-scenario fetch. All edited hardware, links, work and settings remain. Runs/comparisons are cleared. Default scenario/seed fetches and validation use separate abort/epoch protection; stale run/comparison responses are also rejected.
4. **Publish at exact event boundaries.** Trace version 3 (`event-boundaries-v1`) assigns event indices to state records and includes explicit send/queue/hop publications. Every stateful dashboard view uses the same inclusive event prefix. A timestamp locates progress only within an already-published operation, never publishes a later same-time mutation. Atomic commit/release and backward replay are tested.

### Independent cost expectations

For the review's analytic rates (storage 50 B/s, memory 100 B/s, two zero-latency 1,000,000 B/s hops, 10-byte control, 7 ms decision), each parallel endpoint round trip is 4 × 10 / 1,000,000 = 0.00004 s.

| Transition | Independently summed queue-free estimate |
|---|---:|
| Same-pool 100 → 200 B | 0.007 + 0.00004 + 100/50 + 2×100/1e6 + 100/100 = **3.00724 s** |
| Migrate 100 B, no growth | 0.007 + 0.00004 + 100/100 + 2×100/1e6 + 100/100 = **2.00724 s** |
| Migrate 100 B and load 100 extra B | 0.007 + 0.00004 + 2.0002 + 3.0002 = **5.00744 s** |
| Same-pool 100 → 50 B shrink | decision + control, no retained-state copy = **0.00704 s** |
| Migrate and shrink 100 → 50 B | decision + control + 50/100 + 2×50/1e6 + 50/100 = **1.00714 s** |
| Idealized zero reconfiguration | **0 s**, only when the explicit ablation flag is enabled |

Tests compare every planned source/destination/byte amount to executed reads, payload transfers and writes, and compare the uncontended execution body to these sums. Storage migration/preparation have a separate analytic case. The estimate includes one decision allowance; actual decision service executes once before transition.start. Commands execute in parallel, so the estimator uses the maximum queue-free round trip. Actual contention between control flows or other work can add queueing and is intentionally separate.

### Waiting application evidence

The documented fixtures use 100 MiB in the constrained pool, A 80 → 20 MiB, B arriving at 2 s needing 50 MiB, and spare CPU/storage or memory capacity. Both demand/cost give the same observations:

| Pool reclaimed | Old B first start | Fixed B first start | Fixed A finish |
|---|---:|---:|---:|
| Memory | 12.5750 s | **2.5704 s** | 12.3626 s |
| Storage | 12.2150 s | **2.2104 s** | 12.1226 s |

All three original logical chunks complete with no capacity/audit errors. A publishes its 20 MiB reservation before finishing, and transitions have nonzero control duration. Static continues holding its original reservation. [Full before/after evidence](evidence/fixed-reclamation.json), [runnable fixtures](tests/fixtures/shrink-memory-cost.json).

## Pass 2 — full application regression: PASS

Executed checks:

| Check / command | Actual result |
|---|---|
| `.\build.ps1` | Corrected native Windows executable and test executable rebuilt successfully |
| `.\build\distriarch_tests.exe --all` | **80 passed, 0 failed**: all 57 original cases + 23 new cases |
| `.\build\distriarch.exe experiments --output results` | **81 completed, zero audit failures**: 45 main + 27 sensitivity + 9 ablation |
| Native integrity group | Exact deterministic replay, immutable input, matching native policy fingerprints, config/JSON/gzip round trips, work/server/ownership conservation, bounded incomplete runs, release/reuse and discovery all pass |
| `node tests/http_smoke.mjs` | **25 passed, 0 failed**, real HTTP and full native CLI trace equality; health/schema v3, correct HTML/JS bytes, all comparisons and standalone runtime |
| `node --experimental-strip-types --test tests/replay_tests.mjs` | **10 passed, 0 failed**; independent state replay checks every event boundary in the saved trace |
| Frontend TypeScript `tsc --noEmit` and Vite production build | Passed; final bundle `index-DXk7cQQV.js` regenerated from corrected source |
| Actual browser workflows | Custom policies/runs/exports; commit/discovery/release; backward/forward seeking; pause/step/resume/speed/reset; comparison; config and complete-result export; delayed response races all pass |

[Native full output](evidence/fixed-native-tests.txt), [HTTP output](evidence/fixed-http-tests.txt), [replay tests](evidence/fixed-replay-tests.txt), [build output](evidence/fixed-frontend-build.txt), [browser record](evidence/fixed-browser-regressions.json).

The local pnpm policy verifier tried registry lookups inside the network-restricted environment after a package-metadata edit. That retrying invocation was stopped. The final type check and build successfully used the already installed, pinned TypeScript/Vite/Prettier executables directly. No dependency versions were changed. First-time rebuilding elsewhere still needs the documented package/tool installation; running the ZIP does not.

### Actual browser details

The repeatable browser functions are in `tests/browser_regressions.mjs`. A loopback-only `tests/browser_test_proxy.mjs` forced 1.2 s delayed responses; it is used only for tests and is absent from the launch workflow.

- Custom cpu-a 777, edited link latency and cooldown survived static/demand/cost switches. Every exported run contained the preserved entire config. Work/hardware fingerprints matched across all three policies.
- An older validation (cpu-a 888) returned after a later edit (999): 999 remained. An older default-scenario fetch could not restore a previously selected scenario. Delayed run and comparison responses could not restore cleared output. Abort-independent epoch behavior is also covered by the frontend unit test.
- Default phases/cost/11: first composition commits at **event 140**, time **0.512492954441392 s**. Event **139** has the same timestamp and displays **0** compositions; 140 displays **1**. Seeking back restores 0.
- First discovery event **45** becomes visible at 45, not the preceding same-time service event. Release event **1284** atomically clears the composition and reservations; reversing one step restores them.
- All four speed settings leave a paused cursor fixed. A normal Step increments it by exactly one. Resume at 64× reaches event 1285 and 20/20 chunks. Paused/final exports are exactly equal; config export equals the submitted config. Reset clears all evidence/comparisons and disables result/step controls.
- Current default run: **1286 events**, **12.316232899534997 s**, 1 completed / 0 unfinished jobs, 3 reconfigurations. Compare: static 14.619 s; demand/cost 12.316 s.
- Actual final-code browser console error/warning log was empty. A deliberate full reload ensured the rebuilt bundle, rather than a previously cached v2 page, was under test.
- Desktop **1366×900** and narrow **390×844** inspected. Narrow body width = scrollWidth = **385 px**; the results table scrolls internally (**557/305 px**). Controls/topology panels remain within the page. Chart geometry was observed until stable and at least 1.8 s elapsed before chart screenshots.

Genuine screenshots:

- [Before same-time commit](evidence/fixed-before-commit.jpg)
- [After commit](evidence/fixed-after-commit.jpg)
- [Desktop architecture](evidence/fixed-dashboard-laptop.jpg)
- [Settled regenerated sensitivity/ablation charts](evidence/fixed-experiments-laptop.jpg)
- [Narrow controls and metrics](evidence/fixed-dashboard-narrow.jpg)

### Changed experimental findings

**30/81 makespans changed**, 51 stayed numerically equal. All raw traces now use the corrected event schema. Complete old/new data is in [fixed-results-diff.json](evidence/fixed-results-diff.json).

| Mean simulated seconds | Static | Demand | Cost |
|---|---:|---:|---:|
| steady | 9.2471 | 9.3331 | 9.3331 |
| phases | 14.6149 | 12.3114 | 12.3114 |
| concurrent | 10.0118 | 10.5239 | 9.1930 |
| constrained | 14.6150 | 15.7787 | 12.2433 |
| expensive | 10.4714 | 23.7368 | 10.2841 |

Old endpoints now receive required commands/acks, adding small real control costs to some migrations. Expensive/short phases also release an unused accelerator, increasing cost-aware's changes 1 → 2 and demand's 4 → 5; its new control costs reduce cost-aware's mean improvement from 2.03% to **1.79%**. Phases improve **15.76%**, concurrent **8.18%**, constrained **16.23%**. Steady still loses **0.93%**. No result was forced to match old numbers or make adaptation win. Default scenarios have no memory-size growth, so that estimator defect is specifically exercised by analytic/custom fixtures.

Historical migration parity reports/screenshots/counts under `evidence/historical-v2/` are explicitly historical. They are not current v3 parity claims.

## Pass 3 — ready-to-run Windows package after extraction: PASS

The extraction verification is executed with `tests/verify_package.ps1 -ZipPath <archive> -KeepRunning`. It checks every packaged size/SHA-256, excludes caches/objects/retired runtimes/nested ZIPs, runs the included 80 native/10 replay/25 HTTP checks, launches the included `START_WINDOWS.bat`, verifies assets and a complete API run/comparison, and executes all four supplied shrink fixtures. Browser regressions are then run against the launcher-served extracted frontend. A fresh pre-final extraction passed every check: **all 280 manifest entries matched**, **80 native + 10 replay + 25 HTTP/CLI checks passed**, the included batch launcher started the v3 server, production assets loaded, a complete run and all three policies completed, and all four shrink fixtures let B start before A finished. [Executed payload verification](evidence/fixed-package-payload-verification.json). The final archive is verified again in a fresh directory after packaging; that exact ZIP hash and last extraction log are generated outside the ZIP to avoid a self-referential checksum. Functional executable/asset hashes are recorded in the included report and the package manifest.

## Remaining limitations

No functional failure remains in the requested Windows checks. The final ZIP contains fresh native executables and the final production frontend; all original tests and the four fixes are checked again after extraction. The model is illustrative and non-cycle-accurate; documented queue-free heuristics can lose, and live-capacity deadlocks are detected rather than solved by eviction. Windows x64 is the verified runtime/build platform. CMake and Linux/macOS builds are supplied as portability paths but are not tested here. Compiler output includes GCC's conservative dangling-reference warning for a JSON array helper returning a reference into its input (not its temporary string argument); all native/runtime checks pass. No publication or push was performed.

