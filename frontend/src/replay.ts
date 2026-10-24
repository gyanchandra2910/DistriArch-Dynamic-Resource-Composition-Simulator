import type { Composition, Config, Interval, Policy, Result } from "./types";

// A cursor is inclusive: only publications at or before this event exist.
// Time still locates queued work along a service interval, but never publishes
// a later same-time commit, release, registration or transfer completion.
export function projectResult(
  result: Result | null,
  cursor: number,
): Result | null {
  if (!result) return null;
  const seen = (index: number | null) => index !== null && index <= cursor;
  const events = result.events.slice(0, cursor + 1);
  const compositions = result.compositions.filter((c) => seen(c.event_index));
  const discovery = Object.fromEntries(
    Object.entries(result.discovery).filter(([id]) =>
      seen(result.discovery_events[id] ?? null),
    ),
  );
  return {
    ...result,
    events,
    compositions,
    discovery,
    ownership: result.ownership.filter((x) => seen(x.event_index)),
    intervals: result.intervals
      .filter((i) => seen(i.requested_event))
      .map((i) => ({ ...i, finished: seen(i.end_event) })),
    transfers: result.transfers
      .filter((f) => seen(f.start_event))
      .map((f) => ({ ...f, end: seen(f.end_event) ? f.end : null })),
    transitions: result.transitions
      .filter((t) => seen(t.start_event))
      .map((t) => ({ ...t, end: seen(t.end_event) ? t.end : null })),
    jobs: result.jobs.map((j) => {
      const ownEvents = events.filter((e) => e.app === j.id);
      return {
        ...j,
        phases: j.phases.filter((p) => seen(p.event_index)),
        first_start: compositions.find((c) => c.app === j.id)?.time ?? null,
        finished: ownEvents.some((e) => e.kind === "completed")
          ? j.finished
          : null,
        released: ownEvents.some((e) => e.kind === "release")
          ? j.released
          : null,
        completed_chunks: ownEvents.filter((e) => e.kind === "progress").length,
      };
    }),
  };
}

export const intervalActive = (i: Interval, time: number) =>
  i.start <= time && !i.finished;
export const onlineAt = (r: Result | null, id: string) =>
  r?.events.some((e) => e.kind === "online" && e.resource === id) ?? false;
export function currentCompositions(
  result: Result | null,
): Record<string, Composition> {
  const map: Record<string, Composition> = {};
  for (const c of result?.compositions ?? []) map[c.app] = c.composition;
  for (const e of result?.events ?? [])
    if (e.kind === "release") delete map[e.app];
  return map;
}

export function withPolicy(
  config: Config | null,
  policy: Policy,
): Config | null {
  return config ? { ...config, policy } : null;
}

// Identity/epoch checks also protect against responses from transports that
// cannot honor AbortController, including validation/default-config races.
export class RequestEpoch {
  private value = 0;
  begin() {
    return ++this.value;
  }
  current(ticket: number) {
    return ticket === this.value;
  }
}
