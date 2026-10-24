#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <vector>
#include "json.hpp"

namespace distriarch {
using Json = nlohmann::json;
using Bytes = std::int64_t;
using Callback = std::function<void()>;
inline constexpr Bytes MiB = 1024 * 1024;
inline constexpr double epsilon = 1e-9;

struct Resource {
    std::string id, kind;
    Bytes capacity{};
    double rate{}, available_at{};
    std::string capability = "general";
};
struct Link {
    std::string id, a, b;
    double bandwidth{}, latency{};
};
struct Phase {
    std::string name;
    int chunks{};
    double cpu{}, accelerator{};
    Bytes memory{}, read{}, write{}, acc_input{}, acc_output{}, working_set{}, storage_size{};
    std::string capability = "tensor";
};
struct Workload {
    std::string id, name;
    double arrival{};
    std::vector<Phase> phases;
};
struct Settings {
    double interval = .8, decision = .004, registration = .002;
    Bytes control_bytes = 512;
    double preparation = .035, cooldown = 1.5, minimum_benefit = .04, overhead_margin = 1.2;
    bool idealized_zero_reconfiguration = false;
};
struct Configuration {
    std::string name, description, policy = "cost";
    std::uint32_t seed = 11;
    std::vector<Resource> resources;
    std::vector<Link> links;
    std::vector<Workload> workloads;
    Settings settings;
    double horizon = 240;
    std::uint64_t max_events = 100000;
};
struct Composition {
    std::string compute;
    int cores{};
    std::string memory;
    Bytes memory_bytes{};
    std::string storage;
    Bytes storage_bytes{};
    std::optional<std::string> accelerator;
    // Order matters: commands are submitted CPU, memory, storage, accelerator.
    std::vector<std::pair<std::string, Bytes>> holdings() const;
    std::string tie_key() const;
    bool operator==(const Composition&) const = default;
};
struct Observation {
    std::string app;
    Phase phase; // Only this phase is exposed. No workload/future phase reference.
    int remaining_chunks{};
    std::optional<Composition> current;
    double last_change{}, waiting_since{};
};
struct Job {
    Workload spec;
    std::size_t phase_index{};
    int chunk_index{}, completed_chunks{};
    std::string status = "not_arrived";
    std::optional<Composition> composition;
    std::optional<double> first_start, finished, released;
    double last_change = -1e9, waiting_since{};
    bool review = false, decision_pending = false;
    Json history = Json::array();
    Observation observation() const;
};

void to_json(Json&, const Resource&);
void to_json(Json&, const Link&);
void to_json(Json&, const Phase&);
void to_json(Json&, const Workload&);
void to_json(Json&, const Settings&);
void to_json(Json&, const Configuration&);
void to_json(Json&, const Composition&);
Configuration parse_config(const Json&);
Json scenario_catalog();
Configuration scenario(const std::string& name = "phases", std::uint32_t seed = 11,
                       const std::string& policy = "cost");
std::string digest(const Json&);
Json read_json(const std::string& path);
void write_json(const std::string& path, const Json&, bool gzip = false);
}
