export type Policy = "static" | "demand" | "cost";
export type Kind = "compute" | "memory" | "storage" | "accelerator";
export interface Resource {
  id: string;
  kind: Kind;
  capacity: number;
  rate: number;
  available_at: number;
  capability: string;
}
export interface Link {
  id: string;
  a: string;
  b: string;
  bandwidth: number;
  latency: number;
}
export interface Phase {
  name: string;
  chunks: number;
  cpu: number;
  memory: number;
  read: number;
  write: number;
  accelerator: number;
  acc_input: number;
  acc_output: number;
  working_set: number;
  storage_size: number;
  capability: string;
}
export interface Workload {
  id: string;
  name: string;
  arrival: number;
  phases: Phase[];
}
export interface Config {
  name: string;
  description: string;
  seed: number;
  policy: Policy;
  resources: Resource[];
  links: Link[];
  workloads: Workload[];
  settings: Record<string, number | boolean>;
  horizon: number;
  max_events: number;
}
export interface Composition {
  compute: string;
  cores: number;
  memory: string;
  memory_bytes: number;
  storage: string;
  storage_bytes: number;
  accelerator: string | null;
}
export interface Event {
  index: number;
  time: number;
  kind: string;
  app: string;
  message: string;
  completed_chunks?: number;
  phase?: number;
  resource?: string;
  benefit?: number;
  estimated_overhead?: number;
  accepted?: boolean;
}
export interface Interval {
  requested_event: number;
  end_event: number | null;
  finished?: boolean;
  resource: string;
  app: string;
  kind: string;
  operation: string;
  start: number;
  end: number;
  requested: number;
  amount: number;
  bytes: number;
  transfer?: number;
  from_node?: string;
  to_node?: string;
}
export interface Transfer {
  start_event: number;
  end_event: number | null;
  id: number;
  src: string;
  dst: string;
  bytes: number;
  app: string;
  purpose: string;
  start: number;
  end: number | null;
}
export interface Job {
  id: string;
  name: string;
  arrival: number;
  status: string;
  first_start: number | null;
  finished: number | null;
  released: number | null;
  phases: { time: number; phase: number; name: string; event_index: number }[];
  completed_chunks: number;
  total_chunks: number;
}
export interface ResourceMetric {
  kind: Kind;
  capacity: number;
  peak_reserved: number;
  reservation_utilization: number;
  service_utilization: number;
}
export interface JobMetric extends Job {
  flow_time: number | null;
  initial_wait: number | null;
  queue_wait: number;
  unfinished_age: number | null;
}
export interface Summary {
  completed_jobs: number;
  unfinished_jobs: number;
  completed_chunks: number;
  total_chunks: number;
  makespan: number;
  throughput_jobs_per_s: number;
  throughput_chunks_per_s: number;
  bytes_transferred: number;
  link_byte_hops: number;
  reconfiguration_count: number;
  reconfiguration_time: number;
  reconfiguration_bytes: number;
  manager_service_time: number;
  manager_utilization: number;
  management_bytes: number;
  progress_jain: number;
  max_initial_wait: number | null;
  starvation_observations: string[];
  resources: Record<string, ResourceMetric>;
  links: Record<string, number>;
  jobs: JobMetric[];
}
export interface Result {
  version: number;
  trace_schema: string;
  config: Config;
  status: string;
  elapsed: number;
  processed_events: number;
  workload_hash: string;
  hardware_hash: string;
  events: Event[];
  intervals: Interval[];
  transfers: Transfer[];
  ownership: {
    event_index: number;
    time: number;
    resource: string;
    app: string;
    amount: number;
    used: number;
  }[];
  compositions: {
    time: number;
    app: string;
    composition: Composition;
    event_index: number;
  }[];
  transitions: {
    start_event: number;
    end_event: number | null;
    app: string;
    start: number;
    end: number | null;
    reconfiguration: boolean;
    old: Composition | null;
    new: Composition;
  }[];
  discovery: Record<string, number>;
  discovery_events: Record<string, number>;
  jobs: Job[];
  summary: Summary;
}
export interface Comparison {
  policy: Policy;
  status: string;
  summary: Summary;
  workload_hash: string;
  hardware_hash: string;
}
export interface Aggregate {
  group: string;
  variant: string;
  policy: Policy;
  n: number;
  mean: number;
  minimum: number;
  maximum: number;
  sample_sd: number;
  mean_improvement_pct: number;
  mean_reconfigurations: number;
}
export interface Experiments {
  run_count: number;
  seeds: number[];
  aggregate: Aggregate[];
  label?: string;
}
export interface Scenario {
  id: string;
  name: string;
  description: string;
}
