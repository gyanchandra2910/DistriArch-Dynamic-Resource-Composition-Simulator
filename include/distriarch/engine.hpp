#pragma once
#include "model.hpp"
#include <memory>
#include <tuple>

namespace distriarch {
class Clock {
    struct Event {
        double time;
        std::uint64_t sequence;
        Callback callback;
    };
    struct Later {
        bool operator()(const Event& a, const Event& b) const {
            return std::tie(a.time,a.sequence) > std::tie(b.time,b.sequence);
        }
    };
    std::priority_queue<Event,std::vector<Event>,Later> events_;
    std::uint64_t sequence_ = 0;
public:
    double now = 0;
    std::uint64_t processed = 0;
    void at(double when, Callback callback);
    void later(double delay, Callback callback);
    void step();
    bool empty() const { return events_.empty(); }
    double next_time() const { return events_.top().time; }
};
class Simulator;
struct StateMove { std::string source, destination; Bytes bytes; };
struct TransitionPlan {
    std::vector<std::string> control_targets;
    std::vector<StateMove> moves;
    std::optional<std::string> prepare_accelerator;
};
class Network {
    Simulator& sim_;
    std::map<std::string,Link> links_;
    std::map<std::string,double> free_;
    std::map<std::string,std::vector<std::pair<std::string,std::string>>> graph_;
    using RouteKey = std::tuple<std::string,std::string,Bytes>;
    std::map<RouteKey,std::vector<std::string>> cache_;
    struct Flow;
    void hop(std::shared_ptr<Flow>, std::size_t, const std::string&);
public:
    Json transfers = Json::array();
    explicit Network(Simulator&);
    std::vector<std::string> path(const std::string&, const std::string&, Bytes);
    double estimate(const std::string&, const std::string&, Bytes);
    void send(const std::string& src, const std::string& dst, Bytes bytes,
              const std::string& app, const std::string& purpose, Callback done,
              bool control = false);
};
class Controller {
    Simulator& sim_;
public:
    explicit Controller(Simulator& sim) : sim_(sim) {}
    std::vector<Composition> candidates(const Observation&) const;
    double chunk_cost(const Observation&, const Composition&) const;
    double overhead(const Composition&, const Composition&) const;
    std::pair<std::optional<Composition>,Json> choose(const Observation&) const;
};
class Simulator {
    using Task = std::function<void(Callback)>;
    void sequence(std::vector<Task>, Callback);
    void sequence_step(std::shared_ptr<std::vector<Task>>,std::size_t,Callback);
public:
    Configuration config;
    Clock clock;
    std::map<std::string,Resource> resources;
    std::set<std::string> discovered;
    Json discovery_times = Json::object();
    Json discovery_events = Json::object();
    std::map<std::string,std::map<std::string,Bytes>> owners;
    Json ledger = Json::array(), events = Json::array(), intervals = Json::array();
    Json transitions = Json::array(), compositions = Json::array();
    std::vector<Job> jobs;
    std::map<std::string,std::vector<double>> lanes;
    Network network;
    Controller controller;
    std::string status = "ready";
    explicit Simulator(Configuration);
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    void log(const std::string& kind,const std::string& app,const std::string& message,Json data = Json::object());
    void service(const std::string& rid,double duration,const std::string& app,
                 const std::string& operation,Callback done,int amount = 1);
    void own(const std::string& rid,const std::string& app,Bytes amount);
    void release(Job&);
    void register_resource(const Resource&);
    void arrive(Job&);
    void phase_event(Job&);
    void wake_waiters();
    void request_decision(Job&);
    void tick();
    void transition(Job&, const Composition&);
    TransitionPlan transition_plan(const std::optional<Composition>&, const Composition&) const;
    void move(const std::string&,const std::string&,Bytes,const std::string&,const std::string&,Callback);
    void start_chunk(Job&);
    void chunk_done(Job&);
    Json run();
    Json result() const;
};
Json run(const Configuration&);
Json summarize(const Json&);
std::vector<std::string> audit(const Json&);
Json experiments(const std::string& output = "results");
void plot_results(const Json&,const std::string& output);
int serve(const std::string& root,int port);
}
