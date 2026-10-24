#include "distriarch/engine.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace distriarch {
namespace {
template<class T> Json nullable(const std::optional<T>& v) { return v?Json(*v):Json(nullptr); }
Bytes holding(const Composition& c,const std::string& id) { for (const auto& [r,n]:c.holdings()) if (r==id) return n;return 0; }
Bytes used(const std::map<std::string,Bytes>& owners) { Bytes n=0;for (const auto& [id,v]:owners) n+=v;return n; }
bool terminal(const Job& j) { return j.status=="completed"||j.status=="infeasible"; }
}
void Clock::at(double when,Callback callback) {
    if (!std::isfinite(when)||when<now-epsilon) throw std::invalid_argument("Cannot schedule a nonfinite or past event");
    events_.push({when,sequence_++,std::move(callback)});
}
void Clock::later(double delay,Callback cb) { at(now+delay,std::move(cb)); }
void Clock::step() { if (empty()) throw std::logic_error("Empty event queue");auto e=events_.top();events_.pop();now=e.time;++processed;e.callback(); }

struct Network::Flow {
    std::size_t id;std::vector<std::string> route;std::string src,dst,app,purpose;Bytes size;Callback done;
};
Network::Network(Simulator& s):sim_(s) {
    for (const auto& l:s.config.links) { links_[l.id]=l;free_[l.id]=0;graph_[l.a].emplace_back(l.b,l.id);graph_[l.b].emplace_back(l.a,l.id); }
    for (auto& [id,adj]:graph_) std::sort(adj.begin(),adj.end());
}
std::vector<std::string> Network::path(const std::string& src,const std::string& dst,Bytes size) {
    const RouteKey key{src,dst,size};if (cache_.contains(key)) return cache_.at(key);
    using Entry=std::tuple<double,std::string,std::vector<std::string>>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> todo;
    todo.emplace(0,src,std::vector<std::string>{});std::set<std::string> seen;
    while (!todo.empty()) {
        auto [cost,node,route]=todo.top();todo.pop();
        if (node==dst) return cache_[key]=route;
        if (!seen.insert(node).second) continue;
        for (const auto& [next,lid]:graph_[node]) if (!seen.contains(next)) {
            const auto& l=links_.at(lid);auto extended=route;extended.push_back(lid);
            todo.emplace(cost+l.latency+static_cast<double>(size)/l.bandwidth,next,std::move(extended));
        }
    }
    throw std::invalid_argument("No path from "+src+" to "+dst);
}
double Network::estimate(const std::string& src,const std::string& dst,Bytes size) {
    if (!size) return 0;double result=0;
    for (const auto& id:path(src,dst,size)) { const auto& l=links_.at(id);result+=l.latency+static_cast<double>(size)/l.bandwidth; }return result;
}
void Network::send(const std::string& src,const std::string& dst,Bytes size,const std::string& app,const std::string& purpose,Callback done,bool control) {
    if (size<0) throw std::invalid_argument("Negative transfer size");
    if (!size||src==dst) { done();return; }
    auto f=std::make_shared<Flow>(Flow{transfers.size(),path(src,dst,size),src,dst,app,purpose,size,std::move(done)});
    transfers.push_back({{"id",f->id},{"src",src},{"dst",dst},{"bytes",size},{"app",app},{"purpose",purpose},{"control",control},{"start",sim_.clock.now},{"end",nullptr},{"start_event",sim_.events.size()},{"end_event",nullptr}});
    sim_.log("send",app,purpose+" transfer submitted",{{"transfer",f->id},{"bytes",size}});
    hop(f,0,src);
}
void Network::hop(std::shared_ptr<Flow> f,std::size_t index,const std::string& node) {
    if (index==f->route.size()) {
        transfers[f->id]["end"]=sim_.clock.now;
        transfers[f->id]["end_event"]=sim_.events.size();
        sim_.log("transfer",f->app,f->purpose+": "+f->src+" \xe2\x86\x92 "+f->dst,{{"transfer",f->id},{"bytes",f->size}});f->done();return;
    }
    const auto lid=f->route[index];const auto& l=links_.at(lid);const auto next=l.a==node?l.b:l.a;
    const double start=std::max(sim_.clock.now,free_.at(lid));const double end=start+l.latency+static_cast<double>(f->size)/l.bandwidth;
    free_[lid]=end;
    const auto interval=sim_.intervals.size();
    sim_.intervals.push_back({{"resource",lid},{"app",f->app},{"kind","link"},{"operation",f->purpose},{"start",start},{"end",end},{"requested",sim_.clock.now},{"amount",1},{"bytes",f->size},{"transfer",f->id},{"from_node",node},{"to_node",next},{"requested_event",sim_.events.size()},{"end_event",nullptr}});
    sim_.log("queue",f->app,f->purpose+" queued on "+lid,{{"resource",lid},{"interval",interval}});
    sim_.clock.at(end,[this,f,index,next,interval,lid]{
        sim_.intervals[interval]["end_event"]=sim_.events.size();
        sim_.log("hop",f->app,f->purpose+" hop completed on "+lid,{{"resource",lid},{"interval",interval},{"transfer",f->id}});
        hop(f,index+1,next);
    });
}
std::vector<Composition> Controller::candidates(const Observation& o) const {
    auto& s=sim_;const auto& p=o.phase;std::vector<const Resource*> cpus,mems,stores;std::vector<std::optional<std::string>> accs={std::nullopt};
    auto available=[&](const Resource& r) { const auto& owners=s.owners.at(r.id);return r.capacity-used(owners)+(owners.contains(o.app)?owners.at(o.app):0); };
    for (const auto& id:s.discovered) {
        const auto& r=s.resources.at(id);
        if (r.kind=="compute") cpus.push_back(&r);
        if (r.kind=="memory"&&available(r)>=p.working_set) mems.push_back(&r);
        if (r.kind=="storage"&&available(r)>=p.storage_size) stores.push_back(&r);
        if (p.accelerator&&r.kind=="accelerator"&&r.capability==p.capability&&available(r)>=1) accs.push_back(r.id);
    }
    std::vector<Composition> out;
    for (auto c:cpus) for (auto m:mems) for (auto d:stores) for (const auto& a:accs) for (int cores:{1,2,4})
        if (available(*c)>=cores) out.push_back({c->id,cores,m->id,p.working_set,d->id,p.storage_size,a});
    return out;
}
double Controller::chunk_cost(const Observation& o,const Composition& c) const {
    const auto& p=o.phase;auto& s=sim_;const auto& cpu=s.resources.at(c.compute);const auto& mem=s.resources.at(c.memory);const auto& store=s.resources.at(c.storage);
    auto net=[&](const std::string& a,const std::string& b,Bytes n){return s.network.estimate(a,b,n);};
    double cost=p.cpu/(cpu.rate*c.cores);
    if (p.read) cost+=net(c.compute,c.storage,64)+static_cast<double>(p.read)/store.rate+net(c.storage,c.compute,p.read);
    if (p.memory) cost+=net(c.compute,c.memory,p.memory/2)+static_cast<double>(p.memory)/mem.rate+net(c.memory,c.compute,p.memory-p.memory/2);
    if (p.accelerator) {
        if (c.accelerator) {cost+=net(c.compute,*c.accelerator,p.acc_input)+p.accelerator/s.resources.at(*c.accelerator).rate;cost+=net(*c.accelerator,c.compute,p.acc_output);}
        else cost+=p.accelerator/(cpu.rate*c.cores);
    }
    if (p.write) cost+=net(c.compute,c.storage,p.write)+static_cast<double>(p.write)/store.rate;
    return cost;
}
double Controller::overhead(const Composition& old,const Composition& target) const {
    auto& s=sim_;const auto& cfg=s.config.settings;
    if (cfg.idealized_zero_reconfiguration) return 0;
    const auto plan=s.transition_plan(old,target);
    // Commands/acks are submitted in parallel. The estimate deliberately excludes
    // unpredictable queueing, including contention between those control flows.
    double roundtrip=0;
    for (const auto& id:plan.control_targets)
        roundtrip=std::max(roundtrip,s.network.estimate("manager",id,cfg.control_bytes)+s.network.estimate(id,"manager",cfg.control_bytes));
    double cost=cfg.decision+roundtrip;
    for (const auto& move:plan.moves)
        cost+=static_cast<double>(move.bytes)/s.resources.at(move.source).rate
            +s.network.estimate(move.source,move.destination,move.bytes)
            +static_cast<double>(move.bytes)/s.resources.at(move.destination).rate;
    if (plan.prepare_accelerator) cost+=cfg.preparation;
    return cost;
}
std::pair<std::optional<Composition>,Json> Controller::choose(const Observation& o) const {
    if (o.current&&sim_.config.policy!="static") {
        const auto& old=*o.current;
        // A declared smaller live range can be reclaimed in place, without
        // requiring a speedup for its owner or inspecting another job's future.
        // Growth still goes through feasible candidate selection below.
        if (o.phase.working_set<=old.memory_bytes&&o.phase.storage_size<=old.storage_bytes) {
            auto reclaim=old;reclaim.memory_bytes=o.phase.working_set;reclaim.storage_bytes=o.phase.storage_size;
            if (!o.phase.accelerator) reclaim.accelerator.reset();
            if (!o.phase.cpu&&(!o.phase.accelerator||reclaim.accelerator)) reclaim.cores=1;
            if (reclaim!=old) {
                Json released=Json::object(),waiters=Json::array();
                for (const auto& [id,n]:old.holdings()) if (n>holding(reclaim,id)) released[id]=n-holding(reclaim,id);
                for (const auto& j:sim_.jobs) if (j.spec.id!=o.app&&(j.status=="queued"||j.status=="boundary")) waiters.push_back(j.spec.id);
                return {reclaim,{{"reason","Reclaim unused capacity at a safe boundary"},{"accepted",true},{"benefit",chunk_cost(o,old)-chunk_cost(o,reclaim)},{"estimated_overhead",overhead(old,reclaim)},{"reclaimed",released},{"waiting_applications",waiters}}};
            }
        }
    }
    auto options=candidates(o);
    if (options.empty()) return {std::nullopt,{{"reason","No discovered composition has enough free capacity"},{"accepted",false}}};
    auto rank=[&](const Composition& c) { int changes=0;if (o.current) {const auto& old=*o.current;changes=(c.compute!=old.compute)+(c.memory!=old.memory)+(c.storage!=old.storage)+(c.accelerator!=old.accelerator);}return std::tuple(chunk_cost(o,c)+c.cores*1e-6,changes,c.tie_key()); };
    auto best=*std::min_element(options.begin(),options.end(),[&](const auto& a,const auto& b){return rank(a)<rank(b);});
    if (!o.current) return {best,{{"reason","Initial placement from the revealed phase"},{"accepted",true}}};
    if (best==*o.current) return {o.current,{{"reason","Current placement is already preferred"},{"accepted",false}}};
    const double saving=(chunk_cost(o,*o.current)-chunk_cost(o,best))*o.remaining_chunks;const double overhead_cost=overhead(*o.current,best);const auto& settings=sim_.config.settings;
    auto decision=[&](std::optional<Composition> target,bool accepted,const std::string& reason){return std::make_pair(target,Json{{"reason",reason},{"accepted",accepted},{"benefit",saving},{"estimated_overhead",overhead_cost}});};
    const bool resize=o.phase.working_set>o.current->memory_bytes||o.phase.storage_size>o.current->storage_bytes;
    if (sim_.config.policy=="cost"&&!resize) {
        if (sim_.clock.now-o.last_change<settings.cooldown) return decision(o.current,false,"Cooldown prevents repeated switching");
        if (saving<=overhead_cost*settings.overhead_margin+settings.minimum_benefit) return decision(o.current,false,"Expected benefit does not cover migration and control costs");
    }
    if (saving<=1e-6&&!resize) return decision(o.current,false,"No service-time benefit");
    return decision(best,true,"Observed demand favors a different composition");
}
Simulator::Simulator(Configuration c):config(parse_config(Json(c))),network(*this),controller(*this) {
    for (const auto& r:config.resources) {resources[r.id]=r;owners[r.id]={};lanes[r.id]=std::vector<double>(r.kind=="compute"?static_cast<std::size_t>(r.capacity):1,0);}
    lanes["manager"]={0};jobs.reserve(config.workloads.size());for (const auto& w:config.workloads) {Job j;j.spec=w;j.waiting_since=w.arrival;jobs.push_back(std::move(j));}
}
void Simulator::log(const std::string& kind,const std::string& app,const std::string& message,Json data) { Json e={{"index",events.size()},{"time",clock.now},{"kind",kind},{"app",app},{"message",message}};e.update(data);events.push_back(std::move(e)); }
void Simulator::service(const std::string& rid,double duration,const std::string& app,const std::string& operation,Callback done,int amount) {
    if (!std::isfinite(duration)||duration<0||amount<1) throw std::invalid_argument("Invalid service demand");if (!duration) {done();return;}
    auto& free=lanes.at(rid);if (static_cast<std::size_t>(amount)>free.size()) throw std::logic_error("Insufficient service lanes");
    std::vector<std::size_t> indices(free.size());std::iota(indices.begin(),indices.end(),0);
    std::sort(indices.begin(),indices.end(),[&](auto a,auto b){return std::tie(free[a],a)<std::tie(free[b],b);});
    double start=clock.now;for (int i=0;i<amount;++i) start=std::max(start,free[indices[i]]);
    const double end=start+duration;for (int i=0;i<amount;++i) free[indices[i]]=end;
    const auto interval=intervals.size();
    intervals.push_back({{"resource",rid},{"app",app},{"kind",rid=="manager"?"manager":resources.at(rid).kind},{"operation",operation},{"start",start},{"end",end},{"requested",clock.now},{"amount",amount},{"bytes",0},{"requested_event",events.size()},{"end_event",nullptr}});
    log("queue",app,operation+" queued on "+rid,{{"resource",rid},{"interval",interval}});
    clock.at(end,[=,this]{intervals[interval]["end_event"]=events.size();log("service",app,operation+" completed on "+rid,{{"resource",rid},{"start",start},{"duration",duration},{"interval",interval}});done();});
}
void Simulator::own(const std::string& rid,const std::string& app,Bytes amount) {
    if (!discovered.contains(rid)) throw std::logic_error("Cannot allocate an undiscovered resource");auto& o=owners.at(rid);
    const Bytes total=used(o)-(o.contains(app)?o.at(app):0)+amount;
    if (amount<0||total>resources.at(rid).capacity) throw std::logic_error("Capacity violation on "+rid);
    if (amount) o[app]=amount;else o.erase(app);
    ledger.push_back({{"time",clock.now},{"resource",rid},{"app",app},{"amount",amount},{"used",total},{"event_index",events.size()}});
}
void Simulator::release(Job& j) {
    const auto terminal_status=j.status;j.status="releasing";const auto app=j.spec.id;
    auto published=[this,&j,terminal_status,app]{for (const auto& r:config.resources) if (owners.at(r.id).contains(app)) own(r.id,app,0);j.status=terminal_status;j.released=clock.now;log("release",app,"Completion report processed; resources released for waiting applications");wake_waiters();};
    network.send(j.composition?j.composition->compute:"manager","manager",config.settings.control_bytes,app,"release",[this,app,published]{service("manager",config.settings.registration,app,"release inventory update",published);},true);
}
void Simulator::register_resource(const Resource& r) {
    log("online","",r.id+" is physically available; registering",{{"resource",r.id}});
    network.send(r.id,"manager",config.settings.control_bytes,"","discovery",[this,r]{service("manager",config.settings.registration,"","registration",[this,r]{discovered.insert(r.id);discovery_times[r.id]=clock.now;discovery_events[r.id]=events.size();log("discovery","","Discovered "+r.kind+" "+r.id,{{"resource",r.id}});wake_waiters();});},true);
}
void Simulator::arrive(Job& j) {j.status="queued";log("arrival",j.spec.id,j.spec.name+" arrived");phase_event(j);wake_waiters();}
void Simulator::phase_event(Job& j) {const auto name=j.observation().phase.name;j.history.push_back({{"time",clock.now},{"phase",j.phase_index},{"name",name},{"event_index",events.size()}});log("phase",j.spec.id,"Current phase: "+name,{{"phase",j.phase_index}});}
void Simulator::wake_waiters() {std::vector<Job*> ordered;for (auto& j:jobs) ordered.push_back(&j);std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return std::tie(a->waiting_since,a->spec.id)<std::tie(b->waiting_since,b->spec.id);});for (auto j:ordered) if (j->status=="queued"||j->status=="boundary") request_decision(*j);}
void Simulator::request_decision(Job& j) {
    if (j.decision_pending||(j.status!="queued"&&j.status!="boundary")) return;j.decision_pending=true;
    auto decide=[this,&j]{j.decision_pending=false;if (j.status!="queued"&&j.status!="boundary") return;auto obs=j.observation();auto [target,decision]=controller.choose(obs);decision["observation"]={{"phase",obs.phase},{"remaining_chunks",obs.remaining_chunks},{"current",nullable(obs.current)}};log("decision",j.spec.id,decision.at("reason").get<std::string>(),decision);if (!target) return;if (target==j.composition) start_chunk(j);else transition(j,*target);};
    auto reported=[this,&j,decide]{service("manager",config.settings.decision,j.spec.id,"allocation decision",decide);};
    if (j.composition) network.send(j.composition->compute,"manager",config.settings.control_bytes,j.spec.id,"telemetry",reported,true);else reported();
}
void Simulator::tick() {for (auto& j:jobs) if (config.policy!="static"&&j.status=="running") j.review=true;wake_waiters();clock.later(config.settings.interval,[this]{tick();});}
void Simulator::sequence(std::vector<Task> tasks,Callback done) {sequence_step(std::make_shared<std::vector<Task>>(std::move(tasks)),0,std::move(done));}
void Simulator::sequence_step(std::shared_ptr<std::vector<Task>> tasks,std::size_t index,Callback done) {if (index==tasks->size()) {done();return;}(*tasks)[index]([this,tasks,index,done]{sequence_step(tasks,index+1,done);});}
TransitionPlan Simulator::transition_plan(const std::optional<Composition>& old,const Composition& target) const {
    TransitionPlan plan;
    for (const auto& [id,n]:target.holdings()) plan.control_targets.push_back(id);
    // Old-only endpoints also receive the command/ack before their holdings
    // are released. No destination or retained state is freed before commit.
    if (old) for (const auto& [id,n]:old->holdings())
        if (std::find(plan.control_targets.begin(),plan.control_targets.end(),id)==plan.control_targets.end()) plan.control_targets.push_back(id);
    if (old&&old->storage!=target.storage) plan.moves.push_back({old->storage,target.storage,std::min(old->storage_bytes,target.storage_bytes)});
    if (old&&old->memory!=target.memory) plan.moves.push_back({old->memory,target.memory,std::min(old->memory_bytes,target.memory_bytes)});
    const auto extra=target.memory_bytes-(old?old->memory_bytes:0);
    if (extra>0) plan.moves.push_back({target.storage,target.memory,extra});
    if (target.accelerator&&(!old||old->accelerator!=target.accelerator)) plan.prepare_accelerator=target.accelerator;
    return plan;
}
void Simulator::transition(Job& j,const Composition& requested) {
    const auto target=requested;const auto old=j.composition;const auto app=j.spec.id;const bool reconfig=old.has_value();const std::string purpose=reconfig?"reconfiguration":"allocation",label=reconfig?"Reconfiguration":"Allocation";
    for (const auto& [id,n]:target.holdings()) own(id,app,std::max(old?holding(*old,id):0,n));j.status="transition";const auto index=transitions.size();
    transitions.push_back({{"app",app},{"start",clock.now},{"end",nullptr},{"reconfiguration",reconfig},{"old",nullable(old)},{"new",target},{"start_event",events.size()},{"end_event",nullptr}});
    log(reconfig?"reconfigure":"allocate",app,label+" reserved; old live state retained",{{"composition",target}});
    auto commit=[this,&j,target,reconfig,index,app,label]{for (const auto& r:config.resources) {const auto desired=holding(target,r.id);const auto& o=owners.at(r.id);if ((o.contains(app)?o.at(app):0)!=desired) own(r.id,app,desired);}j.composition=target;if (reconfig) j.last_change=clock.now;if (!j.first_start) j.first_start=clock.now;transitions[index]["end"]=clock.now;transitions[index]["end_event"]=events.size();compositions.push_back({{"time",clock.now},{"app",app},{"composition",target},{"event_index",events.size()}});log("composition",app,label+" committed",{{"composition",target}});j.review=false;start_chunk(j);wake_waiters();};
    const bool ideal=reconfig&&config.settings.idealized_zero_reconfiguration;
    if (ideal) {log("ablation",app,"Idealized zero-cost reconfiguration: state/control/preparation omitted");commit();return;}
    const auto plan=transition_plan(old,target);
    std::vector<Task> tasks;
    for (const auto& item:plan.moves) tasks.push_back([this,item,app,purpose](Callback cb){move(item.source,item.destination,item.bytes,app,purpose,std::move(cb));});
    if (plan.prepare_accelerator) tasks.push_back([this,plan,app](Callback cb){service(*plan.prepare_accelerator,config.settings.preparation,app,"accelerator preparation",std::move(cb));});
    auto controlled=[this,tasks,commit]{sequence(tasks,commit);};auto remaining=std::make_shared<std::size_t>(plan.control_targets.size());
    auto ack=[remaining,controlled]{if (--*remaining==0) controlled();};
    for (const auto& id:plan.control_targets) network.send("manager",id,config.settings.control_bytes,app,purpose,[this,id,app,purpose,ack]{network.send(id,"manager",config.settings.control_bytes,app,purpose,ack,true);},true);
}
void Simulator::move(const std::string& src,const std::string& dst,Bytes size,const std::string& app,const std::string& purpose,Callback done) {
    if (!size) {done();return;}service(src,static_cast<double>(size)/resources.at(src).rate,app,purpose+" state read",[=,this]{network.send(src,dst,size,app,purpose,[=,this]{service(dst,static_cast<double>(size)/resources.at(dst).rate,app,purpose+" state write",done);});});
}
void Simulator::start_chunk(Job& j) {
    if (!j.composition) throw std::logic_error("Chunk without composition");const auto c=*j.composition;const auto p=j.observation().phase;const auto app=j.spec.id;
    if (p.working_set>c.memory_bytes||p.storage_size>c.storage_bytes) {j.status="infeasible";log("infeasible",app,"Fixed composition cannot satisfy revealed phase capacity");release(j);return;}
    j.status="running";std::vector<Task> tasks;
    auto cpu=[this,c,app](double work,const std::string& label,Callback cb){service(c.compute,work/(resources.at(c.compute).rate*c.cores),app,label,std::move(cb),c.cores);};
    if (p.cpu) tasks.push_back([cpu,p](Callback cb){cpu(p.cpu,"CPU execution",std::move(cb));});
    if (p.read) tasks.push_back([=,this](Callback cb){network.send(c.compute,c.storage,64,app,"read request",[=,this]{service(c.storage,static_cast<double>(p.read)/resources.at(c.storage).rate,app,"storage read",[=,this]{network.send(c.storage,c.compute,p.read,app,"storage read",cb);});});});
    if (p.memory) tasks.push_back([=,this](Callback cb){network.send(c.compute,c.memory,p.memory/2,app,"memory access",[=,this]{service(c.memory,static_cast<double>(p.memory)/resources.at(c.memory).rate,app,"memory access",[=,this]{network.send(c.memory,c.compute,p.memory-p.memory/2,app,"memory access",cb);});});});
    if (p.accelerator) {
        if (c.accelerator) tasks.push_back([=,this](Callback cb){network.send(c.compute,*c.accelerator,p.acc_input,app,"accelerator input",[=,this]{service(*c.accelerator,p.accelerator/resources.at(*c.accelerator).rate,app,"accelerator execution",[=,this]{network.send(*c.accelerator,c.compute,p.acc_output,app,"accelerator output",cb);});});});
        else tasks.push_back([cpu,p](Callback cb){cpu(p.accelerator,"accelerator CPU fallback",std::move(cb));});
    }
    if (p.write) tasks.push_back([=,this](Callback cb){network.send(c.compute,c.storage,p.write,app,"storage write",[=,this]{service(c.storage,static_cast<double>(p.write)/resources.at(c.storage).rate,app,"storage write",cb);});});
    sequence(std::move(tasks),[this,&j]{clock.later(0,[this,&j]{chunk_done(j);});});
}
void Simulator::chunk_done(Job& j) {
    ++j.completed_chunks;++j.chunk_index;log("progress",j.spec.id,"Logical chunk completed",{{"phase",j.phase_index},{"chunk",j.chunk_index},{"completed_chunks",j.completed_chunks}});
    const bool changed=j.chunk_index==j.spec.phases.at(j.phase_index).chunks;
    if (changed) {++j.phase_index;j.chunk_index=0;if (j.phase_index==j.spec.phases.size()) {j.status="completed";j.finished=clock.now;log("completed",j.spec.id,j.spec.name+" completed");release(j);return;}phase_event(j);}
    j.status="boundary";j.waiting_since=clock.now;
    if (config.policy!="static"&&(changed||j.review)) {j.review=false;request_decision(j);}else start_chunk(j);
}
Json Simulator::run() {
    status="running";for (const auto& r:config.resources) clock.at(r.available_at,[this,r]{register_resource(r);});for (auto& j:jobs) clock.at(j.spec.arrival,[this,&j]{arrive(j);});clock.later(config.settings.interval,[this]{tick();});
    while (!clock.empty()) {
        if (std::all_of(jobs.begin(),jobs.end(),terminal)) {status=std::all_of(jobs.begin(),jobs.end(),[](const Job& j){return j.status=="completed";})?"completed":"infeasible";break;}
        if (clock.processed>=config.max_events) {status="event_limit";break;}
        if (clock.next_time()>config.horizon) {clock.now=config.horizon;status="timeout";break;}
        clock.step();bool active=false,blocked=true;for (const auto& j:jobs) if (!terminal(j)) {active=true;blocked&=(j.status=="queued"||j.status=="boundary")&&!j.decision_pending;}
        if (active&&discovered.size()==resources.size()&&blocked) {status="stalled";log("stalled","","All arrived requests are infeasible with the discovered capacities");break;}
    }
    if (status=="running") status="stalled";log("end","","Simulation "+status);return result();
}
Json Simulator::result() const {
    Json config_json=config;Json result={{"version",3},{"implementation","C++20"},{"trace_schema","event-boundaries-v1"},{"config",config_json},{"status",status},{"elapsed",clock.now},{"processed_events",clock.processed},{"workload_hash",digest(config_json["workloads"])},{"hardware_hash",digest(Json::array({config_json["resources"],config_json["links"]}))},{"events",events},{"intervals",intervals},{"transfers",network.transfers},{"ownership",ledger},{"compositions",compositions},{"transitions",transitions},{"discovery",discovery_times},{"discovery_events",discovery_events},{"jobs",Json::array()}};
    for (const auto& j:jobs) {int total=0;for (const auto& p:j.spec.phases) total+=p.chunks;result["jobs"].push_back({{"id",j.spec.id},{"name",j.spec.name},{"arrival",j.spec.arrival},{"status",j.status},{"first_start",nullable(j.first_start)},{"finished",nullable(j.finished)},{"released",nullable(j.released)},{"phases",j.history},{"completed_chunks",j.completed_chunks},{"total_chunks",total}});}
    result["summary"]=summarize(result);return result;
}
Json run(const Configuration& c) {return Simulator(c).run();}
}
