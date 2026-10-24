import React, { useEffect, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import {
  Activity,
  ArrowDownToLine,
  ArrowRight,
  BookOpen,
  Check,
  ChevronRight,
  CircleHelp,
  Cpu,
  Database,
  FlaskConical,
  Gauge,
  HardDrive,
  Layers3,
  LoaderCircle,
  Maximize2,
  MemoryStick,
  Network,
  Pause,
  Play,
  RotateCcw,
  Settings2,
  SkipForward,
  Square,
  Terminal,
  Waypoints,
  X,
  Zap,
} from "lucide-react";
import {
  Bar,
  BarChart,
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import type {
  Aggregate,
  Comparison,
  Composition,
  Config,
  Experiments,
  Kind,
  Policy,
  Resource,
  Result,
  Scenario,
} from "./types";
import "./styles.css";
import {
  currentCompositions,
  intervalActive,
  onlineAt,
  projectResult,
  RequestEpoch,
  withPolicy,
} from "./replay";

const policyNames: Record<Policy, string> = {
  static: "Static",
  demand: "Demand-driven",
  cost: "Cost-aware",
};
const colors: Record<string, string> = {
  compute: "#68a9ff",
  memory: "#ae91ff",
  storage: "#efb76b",
  accelerator: "#36cbb0",
  link: "#7a91ae",
  manager: "#c5d6eb",
  static: "#879bb7",
  demand: "#edb768",
  cost: "#36cbb0",
};
const jobColors = ["#49d6bd", "#8bb6ff", "#d2a0fd", "#efbd74"];
const icons = {
  compute: Cpu,
  memory: MemoryStick,
  storage: HardDrive,
  accelerator: Zap,
};
const number = (n: number, d = 2) =>
  Number(n).toLocaleString("en-US", {
    maximumFractionDigits: d,
    minimumFractionDigits: d,
  });
const bytes = (n: number) =>
  n >= 1024 ** 3
    ? `${number(n / 1024 ** 3, 2)} GiB`
    : n >= 1024 ** 2
      ? `${number(n / 1024 ** 2, 1)} MiB`
      : n >= 1024
        ? `${number(n / 1024, 1)} KiB`
        : `${number(n, 0)} B`;
const capacity = (r: Resource, n: number) =>
  r.kind === "compute"
    ? `${n} cores`
    : r.kind === "accelerator"
      ? `${n} device${n === 1 ? "" : "s"}`
      : bytes(n);
async function api<T>(path: string, options?: RequestInit): Promise<T> {
  const response = await fetch(path, options);
  if (!response.ok) {
    const text = await response.text();
    throw new Error(
      `Request failed (${response.status}): ${text.slice(0, 350)}`,
    );
  }
  return response.json();
}
function download(name: string, data: unknown) {
  const url = URL.createObjectURL(
    new Blob([JSON.stringify(data, null, 2)], { type: "application/json" }),
  );
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function App() {
  const [tab, setTab] = useState("overview");
  const [scenarios, setScenarios] = useState<Scenario[]>([]);
  const [scenarioId, setScenario] = useState("phases");
  const [policy, setPolicy] = useState<Policy>("cost");
  const [seed, setSeed] = useState(11);
  const [config, setConfig] = useState<Config | null>(null);
  const [result, setResult] = useState<Result | null>(null);
  const [cursor, setCursor] = useState(-1);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [busy, setBusy] = useState(false);
  const [comparing, setComparing] = useState(false);
  const [comparison, setComparison] = useState<Comparison[]>([]);
  const [saved, setSaved] = useState<Experiments | null>(null);
  const [error, setError] = useState("");
  const [selected, setSelected] = useState("mem-a");
  const [editor, setEditor] = useState(false);
  const [draft, setDraft] = useState("");
  const [logFilter, setLogFilter] = useState("decisions");
  const generation = useRef(0);
  const request = useRef<AbortController | null>(null);
  const compareRequest = useRef<AbortController | null>(null);
  const configRequest = useRef<AbortController | null>(null);
  const configEpoch = useRef(new RequestEpoch());
  const policyRef = useRef<Policy>(policy);
  const validationRequest = useRef<AbortController | null>(null);
  const validationEpoch = useRef(new RequestEpoch());
  function clear() {
    generation.current++;
    request.current?.abort();
    compareRequest.current?.abort();
    validationRequest.current?.abort();
    validationEpoch.current.begin();
    setResult(null);
    setComparison([]);
    setCursor(-1);
    setPlaying(false);
    setBusy(false);
    setComparing(false);
    setError("");
  }
  useEffect(() => {
    const abort = new AbortController();
    Promise.all([
      api<Scenario[]>("/api/scenarios", { signal: abort.signal }),
      api<Experiments>("/api/experiments", { signal: abort.signal }),
    ])
      .then(([s, e]) => {
        setScenarios(s);
        setSaved(e);
      })
      .catch((e) => {
        if (e.name !== "AbortError") setError(e.message);
      });
    return () => abort.abort();
  }, []);
  useEffect(() => {
    clear();
    setConfig(null);
    const abort = new AbortController();
    configRequest.current?.abort();
    configRequest.current = abort;
    const ticket = configEpoch.current.begin();
    api<Config>(`/api/config/${scenarioId}?seed=${seed}`, {
      signal: abort.signal,
    })
      .then((c) => {
        if (configEpoch.current.current(ticket) && !abort.signal.aborted)
          setConfig({ ...c, policy: policyRef.current });
      })
      .catch((e) => {
        if (e.name !== "AbortError" && configEpoch.current.current(ticket))
          setError(e.message);
      });
    return () => abort.abort();
  }, [scenarioId, seed]);
  function changePolicy(next: Policy) {
    policyRef.current = next;
    setPolicy(next);
    setConfig((c) => withPolicy(c, next));
    clear();
    // Invalidate a pending validation, but an initial scenario load may finish
    // using the latest policyRef without replacing any edited configuration.
  }
  useEffect(() => {
    if (!playing || !result) return;
    const timer = window.setInterval(
      () => setCursor((c) => Math.min(c + speed, result.events.length - 1)),
      65,
    );
    return () => clearInterval(timer);
  }, [playing, speed, result]);
  useEffect(() => {
    if (result && cursor >= result.events.length - 1) setPlaying(false);
  }, [cursor, result]);
  const time = result && cursor >= 0 ? (result.events[cursor]?.time ?? 0) : 0;
  const elapsed = result?.elapsed ?? 0;
  const atEnd = !!result && cursor === result.events.length - 1;
  const visible = result?.events.slice(0, cursor + 1) ?? [];
  const displayResult = useMemo(
    () => projectResult(result, cursor),
    [result, cursor],
  );
  const completed = visible.filter((e) => e.kind === "progress").length;
  const total =
    config?.workloads.reduce(
      (sum, w) => sum + w.phases.reduce((s, p) => s + p.chunks, 0),
      0,
    ) ?? 0;
  const traffic =
    displayResult?.transfers
      .filter((f) => f.end !== null && f.end <= time)
      .reduce((n, f) => n + f.bytes, 0) ?? 0;
  const reconfigs =
    displayResult?.transitions.filter((x) => x.reconfiguration).length ?? 0;
  const compositions = useMemo(
    () => currentCompositions(displayResult),
    [displayResult],
  );
  const reservations: Record<string, number> = {};
  for (const x of displayResult?.ownership ?? [])
    reservations[x.resource] = x.used;
  async function run() {
    if (!config) return;
    clear();
    setBusy(true);
    const id = generation.current;
    const abort = new AbortController();
    request.current = abort;
    try {
      const r = await api<Result>("/api/run", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(config),
        signal: abort.signal,
      });
      if (id !== generation.current) return;
      setResult(r);
      setCursor(0);
      setPlaying(true);
    } catch (e) {
      if (
        e instanceof Error &&
        e.name !== "AbortError" &&
        id === generation.current
      )
        setError(e.message);
    } finally {
      if (id === generation.current) setBusy(false);
    }
  }
  async function compare() {
    if (!config) return;
    setComparing(true);
    setError("");
    const id = generation.current;
    const abort = new AbortController();
    compareRequest.current = abort;
    try {
      const r = await api<Comparison[]>("/api/compare", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(config),
        signal: abort.signal,
      });
      if (id === generation.current) setComparison(r);
    } catch (e) {
      if (
        e instanceof Error &&
        e.name !== "AbortError" &&
        id === generation.current
      )
        setError(e.message);
    } finally {
      if (id === generation.current) setComparing(false);
    }
  }
  async function applyDraft() {
    const abort = new AbortController();
    validationRequest.current?.abort();
    validationRequest.current = abort;
    const ticket = validationEpoch.current.begin();
    const identity = { scenarioId, seed, policy };
    try {
      const parsed = { ...JSON.parse(draft), name: scenarioId, seed, policy };
      const validated = await api<Config>("/api/validate", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(parsed),
        signal: abort.signal,
      });
      if (
        !validationEpoch.current.current(ticket) ||
        abort.signal.aborted ||
        identity.policy !== policyRef.current
      )
        return;
      configRequest.current?.abort();
      configEpoch.current.begin();
      clear();
      setConfig(validated);
      setEditor(false);
    } catch (e) {
      if (!abort.signal.aborted && validationEpoch.current.current(ticket))
        setError(e instanceof Error ? e.message : "Invalid JSON");
    }
  }
  const status = busy
    ? "Computing trace"
    : !result
      ? "Ready"
      : playing
        ? "Playing"
        : atEnd
          ? result.status === "completed"
            ? "Completed"
            : result.status
          : "Paused";
  const activeResource =
    config?.resources.find((r) => r.id === selected) ?? config?.resources[0];
  return (
    <div className="shell">
      <aside className="sidebar">
        <a
          className="brand"
          href="#"
          onClick={(e) => {
            e.preventDefault();
            setTab("overview");
          }}
        >
          <span className="brand-icon">
            <Waypoints size={23} />
          </span>
          <span>
            Distri<span className="brand-accent">Arch</span>
            <small>ARCHITECTURE LAB</small>
          </span>
        </a>
        <div className="workspace-label">
          WORKSPACE <span>LOCAL</span>
        </div>
        <nav aria-label="Main navigation">
          {[
            ["overview", Network, "Architecture"],
            ["workloads", Layers3, "Workload trace"],
            ["experiments", FlaskConical, "Experiments"],
            ["guide", BookOpen, "Model & methods"],
          ].map(([key, Icon, label]) => {
            const I = Icon as typeof Network;
            return (
              <button
                aria-label={String(label)}
                title={String(label)}
                key={String(key)}
                className={tab === key ? "nav-item active" : "nav-item"}
                onClick={() => setTab(String(key))}
              >
                <I size={19} />
                <span>{String(label)}</span>
                {tab === key && <ChevronRight size={15} />}
              </button>
            );
          })}
        </nav>
        <div className="sidebar-model">
          <div className="mini-topology">
            <Cpu />
            <span />
            <MemoryStick />
            <span />
            <Zap />
          </div>
          <b>
            Separate resources.
            <br />
            One composed system.
          </b>
          <p>Explore how placement and data movement shape execution.</p>
          <div className="model-tag">
            <span className="dot" /> C++20 event engine
          </div>
        </div>
        <div className="sidebar-footer">
          <span className="avatar">GC</span>
          <div>
            Gyan Chandra<small>CS23I1053 · Option 2</small>
          </div>
        </div>
      </aside>
      <main>
        <header className="topbar">
          <div>
            <span className="breadcrumb">
              DISTRIBUTED COMPUTER ARCHITECTURE
            </span>
            <ChevronRight size={13} />
            <span>Simulator</span>
          </div>
          <span className="simulation-badge">
            <span className="dot" />
            Synthetic simulation
          </span>
        </header>
        <div className="content">
          <section className="page-heading">
            <div>
              <div className="eyebrow">DYNAMIC RESOURCE COMPOSITION</div>
              <h1>
                {tab === "experiments"
                  ? "Measure the trade-offs."
                  : tab === "guide"
                    ? "A model you can inspect."
                    : tab === "workloads"
                      ? "Every stage, accounted for."
                      : "Architecture, in motion."}
              </h1>
              <p>
                {tab === "experiments"
                  ? "Reproducible workloads. Honest comparisons. Every result comes from the engine."
                  : tab === "guide"
                    ? "The assumptions, decision rules and metric definitions behind DistriArch."
                    : "Discover, compose and reconfigure physically separated resources."}
              </p>
            </div>
            <button
              aria-label="Configuration"
              className="button subtle"
              onClick={() => {
                setDraft(JSON.stringify(config, null, 2));
                setEditor(true);
              }}
              disabled={!config}
            >
              <Settings2 size={16} />
              Configuration
            </button>
          </section>
          <section className="control-panel" aria-label="Simulation controls">
            <div className="selection-controls">
              <label>
                SCENARIO
                <select
                  aria-label="Scenario"
                  value={scenarioId}
                  onChange={(e) => setScenario(e.target.value)}
                >
                  {scenarios.map((s) => (
                    <option value={s.id} key={s.id}>
                      {s.name}
                    </option>
                  ))}
                </select>
              </label>
              <label>
                COMPOSITION POLICY
                <select
                  aria-label="Policy"
                  value={policy}
                  onChange={(e) => changePolicy(e.target.value as Policy)}
                >
                  {Object.entries(policyNames).map(([id, label]) => (
                    <option value={id} key={id}>
                      {label}
                    </option>
                  ))}
                </select>
              </label>
              <label className="seed-label">
                SEED
                <input
                  aria-label="Seed"
                  type="number"
                  min="0"
                  max="4294967295"
                  value={seed}
                  onChange={(e) => setSeed(Number(e.target.value))}
                />
              </label>
            </div>
            <div className="run-controls">
              <button
                className="button primary"
                onClick={result && !atEnd ? () => setPlaying(!playing) : run}
                disabled={!config || busy}
              >
                {busy ? (
                  <LoaderCircle size={16} className="spin" />
                ) : playing ? (
                  <Pause size={16} />
                ) : (
                  <Play size={16} />
                )}{" "}
                {busy
                  ? "Running"
                  : playing
                    ? "Pause"
                    : result && !atEnd
                      ? "Resume"
                      : "Run simulation"}
              </button>
              <button
                title="Step to next event"
                aria-label="Step event"
                className="icon-button"
                disabled={!result || atEnd}
                onClick={() => {
                  setPlaying(false);
                  setCursor((c) => c + 1);
                }}
              >
                <SkipForward size={18} />
              </button>
              <button
                title="Reset the current run"
                aria-label="Reset"
                className="icon-button"
                onClick={clear}
                disabled={!result && !busy}
              >
                <RotateCcw size={17} />
              </button>
              <div className="divider" />
              <label className="speed-label">
                PLAYBACK
                <select
                  aria-label="Playback speed"
                  value={speed}
                  onChange={(e) => setSpeed(Number(e.target.value))}
                >
                  <option value="1">1×</option>
                  <option value="4">4×</option>
                  <option value="16">16×</option>
                  <option value="64">64×</option>
                </select>
              </label>
            </div>
          </section>
          {error && (
            <div className="error" role="alert">
              <b>Couldn’t complete the action.</b> {error}
              <button aria-label="Dismiss error" onClick={() => setError("")}>
                <X size={16} />
              </button>
            </div>
          )}
          <div className="scenario-caption">
            <span>
              <CircleHelp size={14} />
              {config?.description ??
                "Loading the shared simulation configuration…"}
            </span>
            <span className={`run-status ${playing ? "live" : ""}`}>
              <span className="dot" />
              {status}
            </span>
          </div>
          <section className="stats" aria-label="Run metrics">
            <Metric
              label="SIMULATED TIME"
              value={number(time, 3)}
              unit="s"
              icon={Gauge}
              foot={
                result
                  ? `of ${number(elapsed, 3)} s · independent of playback`
                  : "The engine clock, not wall-clock time"
              }
            />
            <Metric
              label="COMPLETED WORK"
              value={`${completed}`}
              unit={`/ ${total}`}
              icon={Check}
              foot="Fixed logical chunks across all policies"
            />
            <Metric
              label="COMPOSED SYSTEMS"
              value={String(Object.keys(compositions).length)}
              icon={Layers3}
              foot={`${reconfigs} runtime reconfiguration${reconfigs === 1 ? "" : "s"} so far`}
            />
            <Metric
              label="FABRIC TRAFFIC"
              value={bytes(traffic).split(" ")[0]}
              unit={bytes(traffic).split(" ")[1]}
              icon={Activity}
              foot="Completed end-to-end payload & control bytes"
            />
          </section>
          {(tab === "overview" || tab === "workloads") && (
            <>
              {tab === "overview" && (
                <div className="architecture-grid">
                  <section className="panel topology-panel">
                    <PanelTitle
                      icon={Network}
                      title="Resource topology"
                      subtitle="Physical fabric & logical compositions"
                      right={
                        <span className="tag">
                          FABRIC · {config?.resources.length ?? 8} RESOURCES
                        </span>
                      }
                    />
                    <div className="topology-legend">
                      {Object.entries(icons).map(([k, Icon]) => (
                        <span key={k}>
                          <i style={{ background: colors[k] }} />
                          <Icon size={13} />
                          {k}
                        </span>
                      ))}
                    </div>
                    <Topology
                      config={config}
                      result={displayResult}
                      time={time}
                      reservations={reservations}
                      compositions={compositions}
                      selected={selected}
                      onSelect={setSelected}
                    />
                    <div className="topology-footer">
                      <span>
                        <i className="line-sample" /> Physical link
                      </span>
                      <span>
                        <i className="line-sample dashed" /> Composition
                      </span>
                      <span>
                        <i className="transfer-dot" /> Actual transfer
                      </span>
                      <span className="muted">
                        Select a resource to inspect <ArrowRight size={12} />
                      </span>
                    </div>
                  </section>
                  <section className="panel inspector">
                    <PanelTitle
                      icon={Settings2}
                      title="Resource inspector"
                      subtitle="Capacity, ownership & service"
                    />
                    {activeResource && (
                      <>
                        <div className="inspector-name">
                          <span
                            style={{
                              color: colors[activeResource.kind],
                              background: `${colors[activeResource.kind]}16`,
                            }}
                          >
                            {React.createElement(icons[activeResource.kind], {
                              size: 25,
                            })}
                          </span>
                          <div>
                            <h3>{activeResource.id}</h3>
                            <small>
                              {activeResource.kind.toUpperCase()} POOL
                            </small>
                          </div>
                          <span
                            className={`state-label ${displayResult?.discovery[activeResource.id] !== undefined && displayResult.discovery[activeResource.id] <= time ? "online" : ""}`}
                          >
                            {displayResult?.discovery[activeResource.id] !==
                              undefined &&
                            displayResult.discovery[activeResource.id] <= time
                              ? "Discovered"
                              : !onlineAt(displayResult, activeResource.id)
                                ? "Offline"
                                : "Unregistered"}
                          </span>
                        </div>
                        <div className="inspector-data">
                          <div>
                            <span>Reserved capacity</span>
                            <b>
                              {capacity(
                                activeResource,
                                reservations[activeResource.id] ?? 0,
                              )}
                            </b>
                          </div>
                          <div className="capacity-track">
                            <span
                              style={{
                                width: `${(100 * (reservations[activeResource.id] ?? 0)) / activeResource.capacity}%`,
                                background: colors[activeResource.kind],
                              }}
                            />
                          </div>
                          <div>
                            <span>Physical capacity</span>
                            <b>
                              {capacity(
                                activeResource,
                                activeResource.capacity,
                              )}
                            </b>
                          </div>
                          <div>
                            <span>Service rate</span>
                            <b>
                              {activeResource.kind === "compute"
                                ? `${activeResource.rate} units/core·s`
                                : activeResource.kind === "accelerator"
                                  ? `${activeResource.rate} units/s`
                                  : `${bytes(activeResource.rate)}/s`}
                            </b>
                          </div>
                          <div>
                            <span>Service state</span>
                            <b className="accent">
                              {displayResult?.intervals.some(
                                (i) =>
                                  i.resource === activeResource.id &&
                                  i.start <= time &&
                                  intervalActive(i, time),
                              )
                                ? "Processing"
                                : "Idle"}
                            </b>
                          </div>
                          <div>
                            <span>Available at</span>
                            <b>{number(activeResource.available_at, 2)} s</b>
                          </div>
                          <div>
                            <span>Queued operations</span>
                            <b>
                              {displayResult?.intervals.filter(
                                (i) =>
                                  i.resource === activeResource.id &&
                                  i.requested <= time &&
                                  i.start > time,
                              ).length ?? 0}
                            </b>
                          </div>
                          <div>
                            <span>Service utilization to event</span>
                            <b>
                              {number(
                                (100 *
                                  (displayResult?.intervals
                                    .filter(
                                      (i) => i.resource === activeResource.id,
                                    )
                                    .reduce(
                                      (n, i) =>
                                        n +
                                        Math.max(
                                          0,
                                          Math.min(time, i.end) -
                                            Math.min(time, i.start),
                                        ) *
                                          i.amount,
                                      0,
                                    ) ?? 0)) /
                                  ((activeResource.kind === "compute"
                                    ? activeResource.capacity
                                    : 1) *
                                    Math.max(time, 1e-12)),
                                1,
                              )}
                              %
                            </b>
                          </div>
                        </div>
                        <div className="inspector-owners">
                          <span className="section-label">CURRENT OWNERS</span>
                          {Object.entries(compositions)
                            .filter(([, c]) =>
                              [
                                c.compute,
                                c.memory,
                                c.storage,
                                c.accelerator,
                              ].includes(activeResource.id),
                            )
                            .map(([id]) => (
                              <div key={id}>
                                <span
                                  className="dot"
                                  style={{
                                    background:
                                      jobColors[
                                        (config?.workloads.findIndex(
                                          (w) => w.id === id,
                                        ) ?? 0) % 4
                                      ],
                                  }}
                                />
                                {config?.workloads.find((w) => w.id === id)
                                  ?.name ?? id}
                                <span>{id}</span>
                              </div>
                            ))}
                          {!Object.values(compositions).some((c) =>
                            [
                              c.compute,
                              c.memory,
                              c.storage,
                              c.accelerator,
                            ].includes(activeResource.id),
                          ) && (
                            <p className="muted">
                              No committed owner at this event. Reservations
                              during migration remain visible in the bar.
                            </p>
                          )}
                        </div>
                      </>
                    )}
                    <div className="inventory-list">
                      {config?.resources.map((r) => (
                        <button
                          onClick={() => setSelected(r.id)}
                          className={selected === r.id ? "selected" : ""}
                          key={r.id}
                        >
                          <span
                            className="dot"
                            style={{ background: colors[r.kind] }}
                          />
                          {r.id}
                          <span>{capacity(r, reservations[r.id] ?? 0)}</span>
                        </button>
                      ))}
                    </div>
                  </section>
                </div>
              )}
              {tab === "overview" && (
                <FabricTelemetry
                  config={config}
                  result={displayResult}
                  time={time}
                />
              )}
              <div className="trace-grid">
                <section className="panel">
                  <PanelTitle
                    icon={Layers3}
                    title="Workload & allocation timeline"
                    subtitle="Revealed phases, committed mappings and transition delays"
                    right={<span className="tag">SIMULATED SECONDS</span>}
                  />
                  <Timeline
                    result={displayResult}
                    config={config}
                    time={time}
                  />
                  {result && (
                    <div className="scrubber">
                      <span>{number(time, 3)} s</span>
                      <input
                        aria-label="Event position"
                        type="range"
                        min="0"
                        max={result.events.length - 1}
                        value={Math.max(0, cursor)}
                        onChange={(e) => {
                          setPlaying(false);
                          setCursor(Number(e.target.value));
                        }}
                      />
                      <span>{number(elapsed, 3)} s</span>
                    </div>
                  )}
                  <div className="panel-note">
                    <span className="dot" />
                    Each chunk keeps its original work. Composition changes
                    occur at a chunk boundary.
                  </div>
                </section>
                <section className="panel event-panel">
                  <PanelTitle
                    icon={Terminal}
                    title="Decision journal"
                    subtitle="Why the system changed — or stayed put"
                    right={
                      <select
                        aria-label="Event filter"
                        value={logFilter}
                        onChange={(e) => setLogFilter(e.target.value)}
                      >
                        <option value="decisions">Key events</option>
                        <option value="all">All events</option>
                      </select>
                    }
                  />
                  <div className="event-list" aria-label="Event log">
                    {visible
                      .filter(
                        (e) =>
                          logFilter === "all" ||
                          [
                            "discovery",
                            "phase",
                            "decision",
                            "reconfigure",
                            "allocate",
                            "composition",
                            "completed",
                            "stalled",
                            "release",
                            "end",
                          ].includes(e.kind),
                      )
                      .slice(-45)
                      .reverse()
                      .map((e) => (
                        <div
                          className={`event-item event-${e.kind}`}
                          key={e.index}
                        >
                          <span className="event-glyph">
                            {e.kind === "discovery" ? (
                              <Network size={14} />
                            ) : e.kind === "decision" ? (
                              <Settings2 size={14} />
                            ) : e.kind === "completed" ? (
                              <Check size={14} />
                            ) : (
                              <Activity size={14} />
                            )}
                          </span>
                          <div>
                            <div>
                              <b>{e.kind}</b>
                              <time>{number(e.time, 3)} s</time>
                            </div>
                            <p>
                              {e.app && <strong>{e.app} · </strong>}
                              {e.message}
                            </p>
                            {e.benefit !== undefined && (
                              <small>
                                Benefit {number(e.benefit, 3)} s · estimated
                                overhead {number(e.estimated_overhead ?? 0, 3)}{" "}
                                s
                              </small>
                            )}
                          </div>
                        </div>
                      ))}
                    {!visible.length && (
                      <Empty
                        icon={Terminal}
                        title="An explainable event trail"
                        text="Run a simulation to see registration, allocation and runtime decisions."
                      />
                    )}
                  </div>
                </section>
              </div>
              {tab === "workloads" && (
                <section className="panel">
                  <PanelTitle
                    icon={Activity}
                    title="Operation trace"
                    subtitle="CPU → storage → memory → accelerator → write, with real dependencies"
                  />
                  <OperationTrace result={displayResult} time={time} />
                </section>
              )}
              <div className="comparison-grid">
                <section className="panel">
                  <PanelTitle
                    icon={FlaskConical}
                    title="Compare this workload"
                    subtitle="Identical hardware, arrivals, phases and seed"
                    right={
                      <button
                        className="button small"
                        disabled={!config || comparing}
                        onClick={compare}
                      >
                        {comparing ? (
                          <LoaderCircle className="spin" size={14} />
                        ) : (
                          <FlaskConical size={14} />
                        )}
                        Compare policies
                      </button>
                    }
                  />
                  {comparison.length ? (
                    <>
                      <div className="chart-wrap">
                        <ResponsiveContainer width="100%" height={230}>
                          <BarChart
                            data={comparison.map((r) => ({
                              policy: policyNames[r.policy],
                              execution: r.summary.makespan,
                              reconfiguration: r.summary.reconfiguration_time,
                            }))}
                            margin={{ left: 0, right: 20, bottom: 0, top: 10 }}
                          >
                            <CartesianGrid stroke="#26354a" vertical={false} />
                            <XAxis
                              dataKey="policy"
                              tick={{ fill: "#a3b4cd", fontSize: 12 }}
                              axisLine={false}
                              tickLine={false}
                            />
                            <YAxis
                              tick={{ fill: "#8196b3", fontSize: 11 }}
                              axisLine={false}
                              tickLine={false}
                              unit="s"
                            />
                            <Tooltip
                              contentStyle={{
                                background: "#19263b",
                                border: "1px solid #3b4e68",
                                borderRadius: 8,
                              }}
                            />
                            <Bar
                              dataKey="execution"
                              name="Makespan"
                              fill="#759fe0"
                            />
                            <Bar
                              dataKey="reconfiguration"
                              name="Sum of transition time"
                              fill="#34c9ad"
                              radius={[4, 4, 0, 0]}
                            />
                          </BarChart>
                        </ResponsiveContainer>
                      </div>
                      <div className="comparison-values">
                        {comparison.map((r) => (
                          <div key={r.policy}>
                            <span>{policyNames[r.policy]}</span>
                            <b>{number(r.summary.makespan, 3)} s</b>
                            <small>
                              {r.summary.reconfiguration_count} changes ·{" "}
                              {r.summary.unfinished_jobs} unfinished
                            </small>
                          </div>
                        ))}
                      </div>
                      <p className="panel-note">
                        Transition time is summed across jobs and can overlap.
                        Makespan is elapsed simulation time; these bars are
                        separate metrics.
                      </p>
                    </>
                  ) : (
                    <Empty
                      icon={FlaskConical}
                      title="Let the engine settle the comparison"
                      text="Run all three policies on the current configuration. A faster policy is never assumed."
                    />
                  )}
                </section>
                <section className="panel run-summary">
                  <PanelTitle
                    icon={ArrowDownToLine}
                    title="Run evidence"
                    subtitle="Export inputs and the complete audit trace"
                  />
                  {result ? (
                    <>
                      <div className="evidence-hash">
                        <span>WORKLOAD FINGERPRINT</span>
                        <code>{result.workload_hash}</code>
                      </div>
                      <dl>
                        <div>
                          <dt>Engine result</dt>
                          <dd>{result.status}</dd>
                        </div>
                        <div>
                          <dt>Final makespan</dt>
                          <dd>{number(result.summary.makespan, 3)} s</dd>
                        </div>
                        <div>
                          <dt>Completed / unfinished jobs</dt>
                          <dd>
                            {result.summary.completed_jobs} /{" "}
                            {result.summary.unfinished_jobs}
                          </dd>
                        </div>
                        <div>
                          <dt>Manager service time</dt>
                          <dd>
                            {number(result.summary.manager_service_time, 3)} s
                          </dd>
                        </div>
                        <div>
                          <dt>Reconfiguration time / traffic</dt>
                          <dd>
                            {number(result.summary.reconfiguration_time, 3)} s /{" "}
                            {bytes(result.summary.reconfiguration_bytes)}
                          </dd>
                        </div>
                      </dl>
                      <p className="panel-note">
                        Final engine output. Playback only changes the displayed
                        event cursor.
                      </p>
                    </>
                  ) : (
                    <p className="summary-empty">
                      The engine computes a deterministic trace; pause, speed
                      and step let you inspect its execution.
                    </p>
                  )}
                  <div className="export-buttons">
                    <button
                      className="button"
                      onClick={() =>
                        download(
                          `distriarch-${config?.name}-config.json`,
                          config,
                        )
                      }
                      disabled={!config}
                    >
                      <ArrowDownToLine size={14} />
                      Config
                    </button>
                    <button
                      className="button"
                      onClick={() =>
                        download(
                          `distriarch-${config?.name}-${policy}-result.json`,
                          result,
                        )
                      }
                      disabled={!result}
                    >
                      <ArrowDownToLine size={14} />
                      Result & trace
                    </button>
                  </div>
                </section>
              </div>
            </>
          )}
          {tab === "experiments" && (
            <ExperimentsView
              data={saved}
              onCompare={compare}
              comparison={comparison}
              comparing={comparing}
            />
          )}
          {tab === "guide" && <Guide />}
          <footer className="main-footer">
            <span>
              <Waypoints size={15} />
              DistriArch <span>v3.0 · C++</span>
            </span>
            <span>
              Illustrative model parameters · Synthetic workloads · No hardware
              measurements
            </span>
            <span>Gyan Chandra · CS23I1053</span>
          </footer>
        </div>
      </main>
      {editor && (
        <div className="modal-backdrop">
          <section
            className="config-modal"
            role="dialog"
            aria-modal="true"
            aria-label="Simulation configuration"
          >
            <div className="modal-head">
              <div>
                <h2>Simulation configuration</h2>
                <p>The CLI and dashboard use this same JSON schema.</p>
              </div>
              <button
                aria-label="Close configuration"
                className="icon-button"
                onClick={() => setEditor(false)}
              >
                <X size={20} />
              </button>
            </div>
            <textarea
              aria-label="Configuration JSON"
              spellCheck={false}
              value={draft}
              onChange={(e) => setDraft(e.target.value)}
            />
            <p>
              Units: seconds, bytes, work units. The API validates before
              applying changes. Scenario name, policy and seed follow the
              controls above.
            </p>
            {error && (
              <div className="error" role="alert">
                {error}
              </div>
            )}
            <div className="modal-actions">
              <button className="button" onClick={() => setEditor(false)}>
                Cancel
              </button>
              <button className="button primary" onClick={applyDraft}>
                <Check size={16} />
                Apply configuration
              </button>
            </div>
          </section>
        </div>
      )}
    </div>
  );
}

function Metric({
  label,
  value,
  unit,
  icon: Icon,
  foot,
}: {
  label: string;
  value: string;
  unit?: string;
  icon: typeof Gauge;
  foot: string;
}) {
  return (
    <div className="metric">
      <div className="metric-label">
        {label}
        <Icon size={17} />
      </div>
      <div className="metric-value">
        {value} <span>{unit}</span>
      </div>
      <p>{foot}</p>
    </div>
  );
}
function PanelTitle({
  icon: Icon,
  title,
  subtitle,
  right,
}: {
  icon: typeof Network;
  title: string;
  subtitle: string;
  right?: React.ReactNode;
}) {
  return (
    <div className="panel-title">
      <div className="panel-title-text">
        <span className="title-icon">
          <Icon size={17} />
        </span>
        <div>
          <h2>{title}</h2>
          <p>{subtitle}</p>
        </div>
      </div>
      {right}
    </div>
  );
}
function Empty({
  icon: Icon,
  title,
  text,
}: {
  icon: typeof Network;
  title: string;
  text: string;
}) {
  return (
    <div className="empty">
      <span>
        <Icon size={24} />
      </span>
      <b>{title}</b>
      <p>{text}</p>
    </div>
  );
}

function Topology({
  config,
  result,
  time,
  reservations,
  compositions,
  selected,
  onSelect,
}: {
  config: Config | null;
  result: Result | null;
  time: number;
  reservations: Record<string, number>;
  compositions: Record<string, Composition>;
  selected: string;
  onSelect: (id: string) => void;
}) {
  if (!config)
    return (
      <div className="topology-loading">
        <LoaderCircle size={28} className="spin" />
        Loading topology…
      </div>
    );
  const kinds: Kind[] = ["compute", "memory", "storage", "accelerator"];
  const switches = [...new Set(config.links.flatMap((l) => [l.a, l.b]))].filter(
    (id) => id !== "manager" && !config.resources.some((r) => r.id === id),
  );
  const positions: Record<string, [number, number]> = { manager: [150, 220] };
  switches.forEach((id, i) => {
    positions[id] = [
      switches.length === 1
        ? 520
        : 400 + (i * 245) / Math.max(1, switches.length - 1),
      220,
    ];
  });
  const cards: Record<string, [number, number]> = {};
  for (const [col, kind] of kinds.entries())
    config.resources
      .filter((r) => r.kind === kind)
      .forEach((r, row) => {
        cards[r.id] = [38 + col * 234, 35 + row * 290];
        positions[r.id] = [118 + col * 234, 78 + row * 290];
      });
  // Unknown switch IDs in custom topologies get a stable central location.
  for (const l of config.links)
    for (const id of [l.a, l.b]) if (!positions[id]) positions[id] = [520, 220];
  const viewHeight = Math.max(
    450,
    ...Object.values(cards).map(([, y]) => y + 125),
  );
  return (
    <svg
      className="topology"
      viewBox={`0 0 960 ${viewHeight}`}
      role="img"
      aria-label="Interactive topology of separate compute, memory, storage and accelerator pools"
    >
      <defs>
        <pattern id="grid" width="24" height="24" patternUnits="userSpaceOnUse">
          <circle cx="1" cy="1" r=".7" fill="#30425b" opacity=".55" />
        </pattern>
      </defs>
      <rect width="960" height={viewHeight} fill="url(#grid)" />
      <text x="38" y="20" className="rack-caption">
        RACK A
      </text>
      <text x="38" y="310" className="rack-caption">
        RACK B
      </text>
      {config.links.map((l) => {
        const a = positions[l.a],
          b = positions[l.b];
        const active = result?.intervals.find(
          (i) =>
            i.resource === l.id && i.start <= time && intervalActive(i, time),
        );
        const from = active?.from_node ? positions[active.from_node] : a;
        const to = active?.to_node ? positions[active.to_node] : b;
        return (
          <g key={l.id}>
            <line
              x1={a[0]}
              y1={a[1]}
              x2={b[0]}
              y2={b[1]}
              stroke={active ? "#4cb5ac" : "#30435b"}
              strokeWidth={active ? 2 : 1.3}
            />
            {active && (
              <circle
                cx={
                  from[0] +
                  ((to[0] - from[0]) * (time - active.start)) /
                    (active.end - active.start)
                }
                cy={
                  from[1] +
                  ((to[1] - from[1]) * (time - active.start)) /
                    (active.end - active.start)
                }
                r="4"
                fill="#53e1c7"
              />
            )}
            <title>
              {l.id}: {bytes(l.bandwidth)}/s · {number(l.latency * 1000, 2)} ms
              per hop · {active ? "busy" : "idle"}
            </title>
          </g>
        );
      })}
      {Object.entries(compositions).flatMap(([app, c], index) =>
        [c.memory, c.storage, c.accelerator]
          .filter((id): id is string => !!id)
          .map((id) => {
            const a = positions[c.compute],
              b = positions[id];
            return (
              <path
                key={app + id}
                d={`M${a[0]},${a[1]} Q${(a[0] + b[0]) / 2},${(a[1] + b[1]) / 2 + (a[1] < 200 ? 85 : -85)} ${b[0]},${b[1]}`}
                fill="none"
                stroke={jobColors[index % 4]}
                strokeWidth="1.8"
                strokeDasharray="6 5"
                opacity=".75"
              >
                <title>
                  {app}: {c.compute} composed with {id}
                </title>
              </path>
            );
          }),
      )}
      <g transform="translate(89,198)">
        <rect width="122" height="44" rx="8" fill="#1c2c43" stroke="#415a78" />
        <text
          x="61"
          y="19"
          textAnchor="middle"
          fill="#c8d8ed"
          fontSize="11"
          fontWeight="600"
        >
          RESOURCE MANAGER
        </text>
        <text x="61" y="33" textAnchor="middle" fill="#829bb9" fontSize="9">
          discover · compose · observe
        </text>
      </g>
      {switches.map((id) => (
        <g key={id} transform={`translate(${positions[id][0] - 48},201)`}>
          <rect width="96" height="38" rx="7" fill="#1b2a40" stroke="#3c536f" />
          <text x="48" y="16" fill="#b0c4df" textAnchor="middle" fontSize="10">
            {id.slice(0, 14).toUpperCase()}
          </text>
          <text x="48" y="29" fill="#7b94b2" textAnchor="middle" fontSize="9">
            fabric switch
          </text>
        </g>
      ))}
      <text x="522" y="194" fill="#8aa0bd" textAnchor="middle" fontSize="10">
        {bytes(config.links.find((l) => l.id === "backbone")?.bandwidth ?? 0)}/s
      </text>
      {config.resources.map((r) => {
        const [x, y] = cards[r.id];
        const used = reservations[r.id] ?? 0;
        const known =
          result?.discovery[r.id] !== undefined &&
          result.discovery[r.id] <= time;
        const busy = result?.intervals.some(
          (i) =>
            i.resource === r.id && i.start <= time && intervalActive(i, time),
        );
        const color = colors[r.kind];
        const Icon = icons[r.kind];
        return (
          <g
            key={r.id}
            transform={`translate(${x},${y})`}
            role="button"
            tabIndex={0}
            aria-label={`Inspect ${r.id}`}
            onClick={() => onSelect(r.id)}
            onKeyDown={(e) => {
              if (e.key === "Enter" || e.key === " ") onSelect(r.id);
            }}
            className="resource-node"
          >
            <rect
              width="176"
              height="101"
              rx="10"
              fill={r.id === selected ? "#1e324c" : "#17263b"}
              stroke={r.id === selected ? color : known ? "#3c526f" : "#2b3b51"}
              strokeWidth={r.id === selected ? 1.7 : 1}
            />
            <foreignObject x="13" y="13" width="22" height="22">
              <Icon size={20} color={color} />
            </foreignObject>
            <text x="43" y="28" fill="#e2ebf8" fontSize="14" fontWeight="600">
              {r.id.length > 15 ? r.id.slice(0, 13) + "…" : r.id}
            </text>
            <circle
              cx="158"
              cy="23"
              r="3"
              fill={known ? "#39c3a5" : "#61738d"}
            />
            <text x="14" y="51" fill="#95abc7" fontSize="10">
              {capacity(r, used)} / {capacity(r, r.capacity)}
            </text>
            <rect x="14" y="64" width="148" height="4" rx="2" fill="#293d56" />
            <rect
              x="14"
              y="64"
              width={(148 * used) / r.capacity}
              height="4"
              rx="2"
              fill={color}
            />
            <text x="14" y="86" fill={busy ? color : "#8196b1"} fontSize="10">
              {!onlineAt(result, r.id)
                ? `Online at ${r.available_at}s`
                : known
                  ? busy
                    ? "● Processing"
                    : "Discovered · idle"
                  : "Awaiting registration"}
            </text>
            <title>
              {r.id}: {capacity(r, r.capacity)} · reserved {capacity(r, used)}.
              Click to inspect.
            </title>
          </g>
        );
      })}
      {Object.entries(compositions).map(([app], i) => (
        <g
          key={app}
          transform={`translate(${40 + i * 220},${viewHeight - 10})`}
        >
          <circle r="3" fill={jobColors[i % 4]} />
          <text x="10" y="4" fill="#92a9c7" fontSize="10">
            {app} composition
          </text>
        </g>
      ))}
    </svg>
  );
}

function FabricTelemetry({
  config,
  result,
  time,
}: {
  config: Config | null;
  result: Result | null;
  time: number;
}) {
  const active =
    result?.transfers.filter(
      (f) => f.start <= time && (f.end === null || f.end > time),
    ) ?? [];
  const recent =
    result?.transfers
      .filter((f) => f.end !== null && f.end <= time)
      .slice(-3)
      .reverse() ?? [];
  return (
    <section className="panel">
      <PanelTitle
        icon={Activity}
        title="Fabric activity"
        subtitle="Shared link occupancy, queue depth and actual data movement"
        right={<span className="tag">FIFO · HALF DUPLEX</span>}
      />
      <div className="fabric-grid">
        <div className="link-telemetry">
          {config?.links.map((l) => {
            const intervals =
              result?.intervals.filter((i) => i.resource === l.id) ?? [];
            const utilization =
              intervals.reduce(
                (n, i) =>
                  n +
                  Math.max(0, Math.min(time, i.end) - Math.min(time, i.start)),
                0,
              ) / Math.max(time, 1e-12);
            const queue = intervals.filter(
              (i) => i.requested <= time && i.start > time,
            ).length;
            const busy = intervals.some(
              (i) => i.start <= time && intervalActive(i, time),
            );
            return (
              <div className="link-telemetry-row" key={l.id}>
                <div>
                  <span
                    className="dot"
                    style={{ background: busy ? "#4bd8bb" : "#4b627f" }}
                  />
                  <b>{l.id.replace("edge-", "")}</b>
                  <span>
                    {number(utilization * 100, 1)}% · q{queue}
                  </span>
                </div>
                <div className="link-track">
                  <span style={{ width: `${100 * utilization}%` }} />
                </div>
                <small>
                  {bytes(l.bandwidth)}/s · {number(l.latency * 1000, 1)} ms
                </small>
              </div>
            );
          })}
        </div>
        <div className="transfer-list">
          <span className="section-label">
            {active.length
              ? `${active.length} IN-FLIGHT TRANSFERS`
              : "RECENTLY COMPLETED TRANSFERS"}
          </span>
          {(active.length ? active.slice(0, 5) : recent).map((f) => (
            <div className="transfer-item" key={f.id}>
              <ArrowRight size={15} />
              <div>
                <b>
                  {f.src} → {f.dst}
                </b>
                <small>
                  {f.purpose} · {f.app || "manager"}
                </small>
              </div>
              <span>
                {bytes(f.bytes)}
                <small>
                  {f.end !== null && f.end <= time
                    ? "completed"
                    : result?.intervals.some(
                          (i) =>
                            i.transfer === f.id &&
                            i.start <= time &&
                            intervalActive(i, time),
                        )
                      ? "on link"
                      : "queued / between hops"}
                </small>
              </span>
            </div>
          ))}
          {!active.length && !recent.length && (
            <p>
              Run or step the trace to observe link queues and transfers.
              Occupancy includes serialization and propagation.
            </p>
          )}
          <p className="fabric-explanation">
            Utilization = occupied link-seconds / elapsed simulated seconds. q
            is the number waiting to enter that link at this event.
          </p>
        </div>
      </div>
    </section>
  );
}

function Timeline({
  result,
  config,
  time,
}: {
  result: Result | null;
  config: Config | null;
  time: number;
}) {
  if (!result)
    return (
      <Empty
        icon={Layers3}
        title="See composition unfold"
        text="Phases, data movement and allocation changes appear here as events are played."
      />
    );
  const end = Math.max(result.elapsed, 0.1),
    w = 1000,
    left = 155,
    scale = (w - left - 20) / end;
  const palette = ["#719ddf", "#a38be2", "#d7a36a", "#36b59e"];
  return (
    <div className="timeline-scroll">
      <svg
        className="timeline"
        viewBox={`0 0 ${w} ${55 + result.jobs.length * 76}`}
        role="img"
        aria-label="Workload phase and composition timelines"
      >
        {Array.from({ length: 6 }, (_, i) => i).map((i) => (
          <g key={i}>
            <line
              x1={left + ((i * end) / 5) * scale}
              y1="26"
              x2={left + ((i * end) / 5) * scale}
              y2={45 + result.jobs.length * 76}
              stroke="#2b3b52"
              strokeDasharray="3 4"
            />
            <text
              x={left + ((i * end) / 5) * scale}
              y="17"
              fill="#7f96b4"
              fontSize="11"
              textAnchor="middle"
            >
              {number((i * end) / 5, 1)}s
            </text>
          </g>
        ))}
        {result.jobs.map((j, index) => {
          const y = 38 + index * 76;
          return (
            <g key={j.id}>
              <text
                x="20"
                y={y + 13}
                fill="#d2def0"
                fontSize="12"
                fontWeight="600"
              >
                {j.name.length > 19 ? j.id : j.name}
              </text>
              <text x="20" y={y + 32} fill="#7f96b4" fontSize="10">
                {j.id} · phase / mapping
              </text>
              <rect
                x={left}
                y={y}
                width={end * scale}
                height="44"
                rx="4"
                fill="#162338"
              />
              {j.phases
                .filter((p) => p.time <= time)
                .map((p, i) => {
                  const finish = Math.min(
                    time,
                    j.phases[i + 1]?.time ?? j.finished ?? end,
                  );
                  const width = Math.max(0, (finish - p.time) * scale);
                  return (
                    <g key={i}>
                      <rect
                        x={left + p.time * scale}
                        y={y}
                        width={width}
                        height="22"
                        rx="3"
                        fill={palette[p.phase % 4]}
                        opacity=".82"
                      />
                      {width > 57 && (
                        <text
                          x={left + p.time * scale + 8}
                          y={y + 15}
                          fill="#101c2d"
                          fontSize="11"
                          fontWeight="600"
                        >
                          {p.name}
                        </text>
                      )}
                      <title>
                        {p.name}: {number(p.time, 3)}–{number(finish, 3)} s
                      </title>
                    </g>
                  );
                })}
              {result.compositions
                .filter((c) => c.app === j.id && c.time <= time)
                .map((c, i, all) => {
                  const finish = Math.min(
                    time,
                    all[i + 1]?.time ?? j.released ?? end,
                  );
                  const width = Math.max(0, (finish - c.time) * scale);
                  return (
                    <g key={i}>
                      <rect
                        x={left + c.time * scale}
                        y={y + 27}
                        width={width}
                        height="15"
                        rx="2"
                        fill={jobColors[index % 4]}
                        opacity=".25"
                      />
                      {width > 80 && (
                        <text
                          x={left + c.time * scale + 5}
                          y={y + 38}
                          fill={jobColors[index % 4]}
                          fontSize="9"
                        >
                          {c.composition.compute} ·{" "}
                          {c.composition.accelerator ?? "CPU"}
                        </text>
                      )}
                      <title>{JSON.stringify(c.composition)}</title>
                    </g>
                  );
                })}
              {result.transitions
                .filter((t) => t.app === j.id && t.start <= time)
                .map((t, i) => (
                  <rect
                    key={i}
                    x={left + t.start * scale}
                    y={y + 24}
                    width={Math.max(
                      1,
                      (Math.min(time, t.end ?? time) - t.start) * scale,
                    )}
                    height="20"
                    fill="#f2bf71"
                    opacity=".65"
                  >
                    <title>
                      {t.reconfiguration
                        ? "Reconfiguration"
                        : "Initial allocation"}
                      : {number((t.end ?? time) - t.start, 3)} s
                    </title>
                  </rect>
                ))}
            </g>
          );
        })}
        <line
          x1={left + time * scale}
          x2={left + time * scale}
          y1="25"
          y2={45 + result.jobs.length * 76}
          stroke="#dceafa"
          strokeWidth="1.5"
        />
      </svg>
      <div className="timeline-key">
        {config?.workloads.map((j, i) => (
          <span key={j.id}>
            <i style={{ background: jobColors[i % 4] }} />
            {j.name}
          </span>
        ))}
        <span>
          <i style={{ background: "#f2bf71" }} />
          Allocation / migration
        </span>
      </div>
    </div>
  );
}

function OperationTrace({
  result,
  time,
}: {
  result: Result | null;
  time: number;
}) {
  if (!result)
    return (
      <Empty
        icon={Activity}
        title="Operations have dependencies"
        text="Run a workload to inspect the stage completion intervals."
      />
    );
  const kinds = [
    "compute",
    "memory",
    "storage",
    "accelerator",
    "link",
    "manager",
  ];
  const scale = 800 / Math.max(result.elapsed, 0.1);
  return (
    <div className="timeline-scroll">
      <svg
        className="timeline"
        viewBox="0 0 1000 285"
        role="img"
        aria-label="Resource operation intervals"
      >
        {kinds.map((kind, k) => (
          <g key={kind}>
            <text x="20" y={40 + k * 40} fill="#a6bad4" fontSize="12">
              {kind}
            </text>
            <line
              x1="150"
              x2="950"
              y1={36 + k * 40}
              y2={36 + k * 40}
              stroke="#263a52"
            />
            {result.intervals
              .filter((i) => i.kind === kind && i.start < time)
              .map((i, index) => (
                <rect
                  key={index}
                  x={150 + i.start * scale}
                  y={25 + k * 40}
                  width={Math.max(
                    0.6,
                    (Math.min(time, i.end) - i.start) * scale,
                  )}
                  height="17"
                  fill={colors[kind]}
                  opacity=".65"
                >
                  <title>
                    {i.app} · {i.operation} · {i.resource}: {number(i.start, 3)}
                    –{number(i.end, 3)}s; queue{" "}
                    {number(i.start - i.requested, 3)}s
                  </title>
                </rect>
              ))}
          </g>
        ))}
      </svg>
    </div>
  );
}

function ExperimentsView({
  data,
  onCompare,
  comparison,
  comparing,
}: {
  data: Experiments | null;
  onCompare: () => void;
  comparison: Comparison[];
  comparing: boolean;
}) {
  const main = data?.aggregate.filter((a) => a.group === "main") ?? [];
  const chart = [
    "steady",
    "phases",
    "concurrent",
    "constrained",
    "expensive",
  ].map((variant) => ({
    scenario: variant,
    ...Object.fromEntries(
      ["static", "demand", "cost"].map((p) => [
        p,
        main.find((a) => a.variant === variant && a.policy === p)?.mean,
      ]),
    ),
  }));
  const sensitivity = [24, 90, 360].map((n) => ({
    bandwidth: `${n} MiB/s`,
    ...Object.fromEntries(
      ["static", "demand", "cost"].map((p) => [
        p,
        data?.aggregate.find(
          (a) =>
            a.group === "sensitivity" &&
            a.variant === `backbone-${n}-MiBs` &&
            a.policy === p,
        )?.mean,
      ]),
    ),
  }));
  const ablation = ["static", "demand", "cost"].map((p) => ({
    policy: policyNames[p as Policy],
    modeled: main.find((a) => a.variant === "expensive" && a.policy === p)
      ?.mean,
    idealized: data?.aggregate.find(
      (a) => a.group === "ablation" && a.policy === p,
    )?.mean,
  }));
  return (
    <>
      <div className="experiment-banner">
        <span className="experiment-icon">
          <FlaskConical size={29} />
        </span>
        <div>
          <h2>{data?.run_count ?? 0} reproducible runs</h2>
          <p>
            5 scenarios × 3 policies × 3 seeds, plus bandwidth sensitivity and
            an idealized ablation.
          </p>
        </div>
        <span className="tag">SEEDS {data?.seeds.join(" · ") || "—"}</span>
      </div>
      {!data?.run_count ? (
        <Empty
          icon={FlaskConical}
          title="No saved experiments yet"
          text="Run .\build\distriarch.exe experiments from the project root, then reload."
        />
      ) : (
        <>
          <section className="panel">
            <PanelTitle
              icon={FlaskConical}
              title="Policy comparison"
              subtitle="Mean simulated makespan over three fixed seeds. Lower is faster."
              right={
                <button
                  className="button small"
                  onClick={() => download("distriarch-experiments.json", data)}
                >
                  <ArrowDownToLine size={14} />
                  Export results
                </button>
              }
            />
            <div className="chart-wrap big">
              <ResponsiveContainer width="100%" height={340}>
                <BarChart
                  data={chart}
                  margin={{ top: 20, left: 5, right: 30, bottom: 5 }}
                >
                  <CartesianGrid stroke="#293951" vertical={false} />
                  <XAxis
                    dataKey="scenario"
                    tick={{ fill: "#a6b8d0", fontSize: 12 }}
                    tickLine={false}
                  />
                  <YAxis tick={{ fill: "#8da3bf", fontSize: 11 }} unit="s" />
                  <Tooltip
                    contentStyle={{
                      background: "#18263b",
                      border: "1px solid #3b506c",
                    }}
                  />
                  <Legend />
                  {(["static", "demand", "cost"] as Policy[]).map((p) => (
                    <Bar
                      key={p}
                      dataKey={p}
                      name={policyNames[p]}
                      fill={colors[p]}
                      radius={[4, 4, 0, 0]}
                      maxBarSize={40}
                    />
                  ))}
                </BarChart>
              </ResponsiveContainer>
            </div>
            <div className="table-scroll">
              <table>
                <thead>
                  <tr>
                    <th>Scenario</th>
                    <th>Policy</th>
                    <th>Mean makespan</th>
                    <th>Min–max (s)</th>
                    <th>Sample SD</th>
                    <th>vs. static</th>
                    <th>Changes</th>
                  </tr>
                </thead>
                <tbody>
                  {main.map((a: Aggregate) => (
                    <tr key={a.variant + a.policy}>
                      <td>{a.variant}</td>
                      <td>
                        <span
                          className="dot"
                          style={{ background: colors[a.policy] }}
                        />
                        {policyNames[a.policy]}
                      </td>
                      <td>{number(a.mean, 3)} s</td>
                      <td>
                        {number(a.minimum, 3)}–{number(a.maximum, 3)}
                      </td>
                      <td>{number(a.sample_sd, 3)}</td>
                      <td
                        className={
                          a.mean_improvement_pct >= 0 ? "accent" : "negative"
                        }
                      >
                        {a.mean_improvement_pct > 0 ? "+" : ""}
                        {number(a.mean_improvement_pct, 2)}%
                      </td>
                      <td>{number(a.mean_reconfigurations, 1)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <p className="panel-note">
              Positive improvement means a shorter makespan, matched against the
              same scenario and seed. Three seeds describe variation; no
              statistical significance is claimed.
            </p>
          </section>
          <div className="comparison-grid">
            <section className="panel">
              <PanelTitle
                icon={Network}
                title="Bandwidth sensitivity"
                subtitle="Only the shared backbone bandwidth changes"
              />
              <div className="chart-wrap">
                <ResponsiveContainer width="100%" height={260}>
                  <LineChart
                    data={sensitivity}
                    margin={{ right: 25, left: 5, top: 10 }}
                  >
                    <CartesianGrid stroke="#293951" />
                    <XAxis
                      dataKey="bandwidth"
                      tick={{ fill: "#8da3bf", fontSize: 11 }}
                    />
                    <YAxis tick={{ fill: "#8da3bf", fontSize: 11 }} unit="s" />
                    <Tooltip
                      contentStyle={{
                        background: "#18263b",
                        border: "1px solid #3b506c",
                      }}
                    />
                    <Legend />
                    {(["static", "demand", "cost"] as Policy[]).map((p) => (
                      <Line
                        key={p}
                        type="linear"
                        dataKey={p}
                        stroke={colors[p]}
                        name={policyNames[p]}
                        strokeWidth={2}
                      />
                    ))}
                  </LineChart>
                </ResponsiveContainer>
              </div>
            </section>
            <section className="panel">
              <PanelTitle
                icon={Zap}
                title="What if switching were free?"
                subtitle="Explicitly idealized ablation · expensive/short phases"
              />
              <div className="chart-wrap">
                <ResponsiveContainer width="100%" height={260}>
                  <BarChart
                    data={ablation}
                    margin={{ right: 25, left: 5, top: 10 }}
                  >
                    <CartesianGrid stroke="#293951" vertical={false} />
                    <XAxis
                      dataKey="policy"
                      tick={{ fill: "#8da3bf", fontSize: 11 }}
                    />
                    <YAxis tick={{ fill: "#8da3bf", fontSize: 11 }} unit="s" />
                    <Tooltip
                      contentStyle={{
                        background: "#18263b",
                        border: "1px solid #3b506c",
                      }}
                    />
                    <Legend />
                    <Bar
                      dataKey="modeled"
                      name="Modeled overhead"
                      fill="#879bb7"
                      radius={[4, 4, 0, 0]}
                    />
                    <Bar
                      dataKey="idealized"
                      name="Idealized zero switching cost"
                      fill="#36cbb0"
                      radius={[4, 4, 0, 0]}
                    />
                  </BarChart>
                </ResponsiveContainer>
              </div>
            </section>
          </div>
        </>
      )}
      <section className="panel experiment-custom">
        <div>
          <h2>Compare the selected configuration</h2>
          <p>
            Uses the current controls above and the same engine as the saved
            experiment.
          </p>
        </div>
        <button
          className="button primary"
          disabled={comparing}
          onClick={onCompare}
        >
          <FlaskConical size={16} />
          {comparing ? "Comparing…" : "Run current comparison"}
        </button>
        {comparison.length > 0 && (
          <div className="comparison-values">
            {comparison.map((c) => (
              <div key={c.policy}>
                <span>{policyNames[c.policy]}</span>
                <b>{number(c.summary.makespan, 3)} s</b>
                <small>
                  {c.status} · {c.summary.unfinished_jobs} unfinished
                </small>
              </div>
            ))}
          </div>
        )}
      </section>
    </>
  );
}

function Guide() {
  return (
    <div className="guide-grid">
      <section className="panel prose">
        <PanelTitle
          icon={Waypoints}
          title="One application, a composed system"
          subtitle="Explicit ownership, explicit costs"
        />
        <p>
          Each composition links reserved CPU cores, memory bytes, storage bytes
          and an optional exclusive accelerator. Unregistered resources are
          never allocatable. Data is metadata and byte counts, not large host
          allocations.
        </p>
        <ol>
          <li>
            <b>Discover:</b> an online resource sends a registration message
            through the same constrained fabric, then the manager processes it.
          </li>
          <li>
            <b>Compose:</b> reserve capacity; send commands and
            acknowledgements; load the working set from storage to memory.
          </li>
          <li>
            <b>Execute:</b> fixed chunks contain CPU work, storage reads,
            batched memory access, optional accelerator work and writes.
            Applications overlap; stages within one application are sequential.
          </li>
          <li>
            <b>Reconfigure:</b> wait for a chunk boundary, reserve the
            destination while retaining the source, copy live state and prepare
            the accelerator, then commit and release the source.
          </li>
        </ol>
        <h3>The timing model</h3>
        <code className="equation">
          CPU seconds = work / (cores × per-core rate)
          <br />
          Link seconds = latency + bytes / bandwidth
          <br />
          Service seconds = demand / resource rate
        </code>
        <p>
          Links are half-duplex FIFO servers. Each hop queues when the previous
          hop finishes; propagation occupies the link. This conservative
          store-and-forward abstraction prevents concurrent transfers receiving
          the entire shared bandwidth.
        </p>
      </section>
      <section className="panel prose">
        <PanelTitle
          icon={Settings2}
          title="Three transparent policies"
          subtitle="Every policy sees the same hardware and logical work"
        />
        <h3>Static</h3>
        <p>
          Choose the best feasible placement from the first revealed phase and
          discovered resources. Retain it until completion. If a later phase
          exceeds its reserved capacity, report it as infeasible.
        </p>
        <h3>Demand-driven</h3>
        <p>
          At phase boundaries and periodic reviews, minimize the estimated
          queue-free duration of the current chunk. Reconsider only after the
          running chunk finishes. Ignore migration overhead.
        </p>
        <h3>Cost-aware</h3>
        <p>
          Estimate savings over only the remaining chunks in the revealed phase.
          Accept a change when savings exceed 1.2 × estimated switching cost +
          40 ms, with a 1.5 s cooldown. A capacity resize is mandatory. Oldest
          waiting requests are considered first.
        </p>
        <p>
          Both adaptive policies reclaim smaller memory and storage
          reservations, unused accelerator devices and idle CPU cores at a safe
          boundary. Reclamation pays the normal commands and acknowledgements
          and publishes the ownership change atomically. It does not require a
          speedup for the owner; other applications can use the released
          capacity.
        </p>
        <p>
          The controller sees current phase demands, remaining chunks,
          discovered inventory, present reservations and its last change time.
          It never inspects future phases or arrivals. The heuristic cannot
          predict future congestion and does not guarantee an optimum.
        </p>
      </section>
      <section className="panel prose">
        <PanelTitle
          icon={Gauge}
          title="What the numbers mean"
          subtitle="Simulation seconds, bytes and work units"
        />
        <dl>
          <dt>Makespan / throughput</dt>
          <dd>
            Elapsed simulation time from t=0. Completed jobs or chunks divided
            by that time. Unfinished jobs are reported separately.
          </dd>
          <dt>Queueing delay</dt>
          <dd>
            Sum of operation waits before resource or link service, including
            control operations. Initial wait is arrival to first committed
            composition.
          </dd>
          <dt>Resource utilization</dt>
          <dd>
            CPU busy core-seconds / (physical cores × elapsed time); other
            service servers use busy seconds / elapsed time. Memory/storage
            occupancy is reserved byte-seconds / (capacity × elapsed time).
          </dd>
          <dt>Network traffic</dt>
          <dd>
            Completed end-to-end bytes count once per transfer; link byte-hops
            count each completed hop. Latency is charged once per hop.
          </dd>
          <dt>Fairness</dt>
          <dd>
            Jain’s index on each job’s fraction of logical chunks completed. It
            becomes 1 when all jobs finish; inspect maximum wait and unfinished
            jobs as well.
          </dd>
        </dl>
      </section>
      <section className="panel prose">
        <PanelTitle
          icon={BookOpen}
          title="Scope & assumptions"
          subtitle="Architectural feasibility, not cycle accuracy"
        />
        <p>
          All rates and capacities are illustrative modeling assumptions. This
          is not a benchmark, an implementation of RDMA or a simulation of cache
          coherence, packet-level transport, energy, failures or a real device.
        </p>
        <p>
          Memory accesses are batched into byte transfers. CPU private live
          state resides in the memory pool. Storage data is a synthetic existing
          dataset. At a chunk boundary, memory beyond a newly smaller working
          set is dead; retained bytes are copied during migration.
        </p>
        <p>
          The idealized ablation removes committed reconfiguration’s control
          traffic, state copies and accelerator preparation. Initial discovery,
          initial allocation, workload transfers and periodic manager
          evaluations remain modeled.
        </p>
        <p>
          Playback reads an immutable completed event trace. Pausing, seeking
          and changing speed cannot alter the engine outcome. Run evidence
          always identifies final metrics explicitly. State is shown immediately
          after the selected event. Changes sharing the same simulated time
          appear only when their own event is reached.
        </p>
      </section>
    </div>
  );
}

createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
