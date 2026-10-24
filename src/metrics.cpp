#include "distriarch/engine.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace distriarch {
namespace {
double d(const Json& j,const char* key) {return j.at(key).get<double>();}
std::string str(const Json& j,const char* key) {return j.at(key).get<std::string>();}
}
Json summarize(const Json& r) {
    const double t=d(r,"elapsed"),denom=std::max(t,1e-12);const auto& intervals=r.at("intervals");const auto& jobs=r.at("jobs");
    std::map<std::string,double> busy,waits;
    for (const auto& i:intervals) {busy[str(i,"resource")]+=std::max(0.0,std::min(d(i,"end"),t)-std::min(d(i,"start"),t))*d(i,"amount");waits[str(i,"app")]+=std::max(0.0,std::min(d(i,"start"),t)-std::min(d(i,"requested"),t));}
    Json utilization=Json::object(),links=Json::object();
    for (const auto& resource:r["config"]["resources"]) {
        const auto id=str(resource,"id");double last=0,area=0;Bytes used=0,peak=0;
        for (const auto& x:r["ownership"]) if (x["resource"]==id) {area+=used*(d(x,"time")-last);last=d(x,"time");used=x["used"].get<Bytes>();peak=std::max(peak,used);}
        area+=used*(t-last);const double capacity=d(resource,"capacity");
        utilization[id]={{"kind",resource["kind"]},{"capacity",resource["capacity"]},{"peak_reserved",peak},{"reservation_utilization",area/(capacity*denom)},{"service_utilization",busy[id]/((resource["kind"]=="compute"?capacity:1)*denom)}};
    }
    for (const auto& link:r["config"]["links"]) links[str(link,"id")]=busy[str(link,"id")]/denom;
    Bytes bytes=0,control=0,reconfig_bytes=0,byte_hops=0;
    for (const auto& f:r["transfers"]) if (!f["end"].is_null()&&d(f,"end")<=t) {const auto n=f["bytes"].get<Bytes>();bytes+=n;if (f.value("control",false)) control+=n;if (f["purpose"]=="reconfiguration") reconfig_bytes+=n;}
    for (const auto& i:intervals) if (i["kind"]=="link"&&d(i,"end")<=t) byte_hops+=i["bytes"].get<Bytes>();
    int completed=0,chunks=0,total_chunks=0,reconfigs=0;double progress_sum=0,progress_square=0,reconfig_time=0;std::optional<double> last_finish,max_wait;
    Json latencies=Json::array(),starvation=Json::array();
    for (const auto& j:jobs) {
        chunks+=j["completed_chunks"].get<int>();total_chunks+=j["total_chunks"].get<int>();const double progress=d(j,"completed_chunks")/d(j,"total_chunks");progress_sum+=progress;progress_square+=progress*progress;
        Json latency=j;latency["flow_time"]=nullptr;latency["initial_wait"]=nullptr;latency["unfinished_age"]=nullptr;latency["queue_wait"]=waits[str(j,"id")];
        if (!j["finished"].is_null()) {++completed;const double finish=d(j,"finished");last_finish=std::max(last_finish.value_or(finish),finish);latency["flow_time"]=finish-d(j,"arrival");}
        else {starvation.push_back(j["id"]);if (d(j,"arrival")<=t) latency["unfinished_age"]=t-d(j,"arrival");}
        if (!j["first_start"].is_null()) {const double wait=d(j,"first_start")-d(j,"arrival");latency["initial_wait"]=wait;max_wait=std::max(max_wait.value_or(wait),wait);}
        latencies.push_back(latency);
    }
    for (const auto& x:r["transitions"]) if (x["reconfiguration"].get<bool>()) {++reconfigs;reconfig_time+=std::max(0.0,(x["end"].is_null()?t:d(x,"end"))-d(x,"start"));}
    Json logical=Json::object();
    for (const auto* key:{"cpu","memory","read","write","accelerator","acc_input","acc_output"}) {
        double total=0,done=0;for (const auto& w:r["config"]["workloads"]) for (const auto& p:w["phases"]) total+=d(p,key)*d(p,"chunks");
        for (const auto& e:r["events"]) if (e["kind"]=="progress") for (const auto& w:r["config"]["workloads"]) if (w["id"]==e["app"]) {done+=d(w["phases"].at(e["phase"].get<std::size_t>()),key);break;}
        logical[key]={{"total",total},{"completed",done},{"unfinished",std::max(0.0,total-done)}};
    }
    return {{"completed_jobs",completed},{"unfinished_jobs",jobs.size()-completed},{"completed_chunks",chunks},{"total_chunks",total_chunks},{"makespan",t},{"completed_job_makespan",last_finish?Json(*last_finish):Json(nullptr)},
        {"throughput_jobs_per_s",completed/denom},{"throughput_chunks_per_s",chunks/denom},{"bytes_transferred",bytes},{"link_byte_hops",byte_hops},{"reconfiguration_count",reconfigs},{"reconfiguration_time",reconfig_time},{"reconfiguration_bytes",reconfig_bytes},{"manager_service_time",busy["manager"]},{"manager_utilization",busy["manager"]/denom},{"management_bytes",control},{"logical_work",logical},{"progress_jain",progress_square?progress_sum*progress_sum/(jobs.size()*progress_square):0},{"max_initial_wait",max_wait?Json(*max_wait):Json(nullptr)},{"starvation_observations",starvation},{"resources",utilization},{"links",links},{"jobs",latencies}};
}
std::vector<std::string> audit(const Json& r) {
    std::set<std::string> errors;
    try {
        if (summarize(r)!=r.at("summary")) errors.insert("Summary does not reconcile with audit records");
        std::map<std::string,Json> resources;std::map<std::string,std::map<std::string,Bytes>> owners;
        for (const auto& v:r["config"]["resources"]) resources[str(v,"id")]=v;
        for (const auto& x:r["ownership"]) {
            const auto id=str(x,"resource");if (!resources.contains(id)) {errors.insert("Unknown allocation resource");continue;}
            if (!r["discovery"].contains(id)||d(x,"time")<r["discovery"][id].get<double>()) errors.insert("Undiscovered allocation");
            owners[id][str(x,"app")]=x["amount"].get<Bytes>();Bytes used=0;for (const auto& [app,n]:owners[id]) used+=n;
            if (x["amount"].get<Bytes>()<0||used!=x["used"].get<Bytes>()||used>resources[id]["capacity"].get<Bytes>()) errors.insert("Reservation conservation failure");
        }
        std::map<std::string,Bytes> capacities={{"manager",1}};for (const auto& [id,v]:resources) capacities[id]=v["kind"]=="compute"?v["capacity"].get<Bytes>():1;for (const auto& l:r["config"]["links"]) capacities[str(l,"id")]=1;
        std::map<std::string,std::vector<std::pair<double,Bytes>>> events;
        for (const auto& i:r["intervals"]) {
            const auto id=str(i,"resource");if (!capacities.contains(id)) errors.insert("Unknown service resource");
            if (d(i,"end")<d(i,"start")||d(i,"start")<d(i,"requested")||d(i,"amount")<1) errors.insert("Invalid service interval");
            if (d(i,"end")>d(i,"start")) {const auto amount=i["amount"].get<Bytes>();events[id].emplace_back(d(i,"start"),amount);events[id].emplace_back(d(i,"end"),-amount);}
        }
        for (auto& [id,ev]:events) {std::sort(ev.begin(),ev.end());Bytes used=0;for (const auto& [time,delta]:ev) {used+=delta;if (used<0||used>capacities[id]) errors.insert("Service capacity violation: "+id);}}
        for (const auto& j:r["jobs"]) {
            int count=0;for (const auto& e:r["events"]) if (e["kind"]=="progress"&&e["app"]==j["id"]) {++count;if (e["completed_chunks"]!=count) errors.insert("Nonsequential chunk progress");}
            const int n=j["completed_chunks"].get<int>(),total=j["total_chunks"].get<int>();
            if (count!=n||n<0||n>total) errors.insert("Lost, duplicated or negative work");if (j["status"]=="completed"&&n!=total) errors.insert("Completion without all work");
        }
        if (r.value("version",0)>=3) {
            auto publication=[&](const Json& index,double time,const std::set<std::string>& kinds) {
                if (!index.is_number_unsigned()&&!index.is_number_integer()) {errors.insert("Missing event publication index");return;}
                const auto n=index.get<std::int64_t>();
                if (n<0||static_cast<std::size_t>(n)>=r["events"].size()) {errors.insert("Invalid event publication index");return;}
                const auto& event=r["events"][static_cast<std::size_t>(n)];
                if (event["time"].get<double>()!=time||(!kinds.empty()&&!kinds.contains(event["kind"].get<std::string>()))) errors.insert("Event publication does not match its record");
            };
            double previous=0;std::size_t index=0;
            for (const auto& e:r["events"]) {if (e["index"]!=index++||d(e,"time")<previous) errors.insert("Unstable event ordering");previous=d(e,"time");}
            for (const auto& c:r["compositions"]) publication(c.at("event_index"),d(c,"time"),{"composition"});
            for (const auto& x:r["ownership"]) {
                publication(x.at("event_index"),d(x,"time"),{"allocate","reconfigure","composition","release"});
                const auto id=str(x,"resource");
                if (!r["discovery_events"].contains(id)||x["event_index"].get<std::int64_t>()<r["discovery_events"][id].get<std::int64_t>()) errors.insert("Allocation precedes discovery event");
            }
            for (auto it=r["discovery"].begin();it!=r["discovery"].end();++it) publication(r["discovery_events"].at(it.key()),it.value().get<double>(),{"discovery"});
            for (const auto& i:r["intervals"]) {publication(i.at("requested_event"),d(i,"requested"),{"queue"});if (!i["end_event"].is_null()) publication(i["end_event"],d(i,"end"),{"service","hop"});}
            for (const auto& f:r["transfers"]) {publication(f.at("start_event"),d(f,"start"),{"send"});if (!f["end_event"].is_null()) publication(f["end_event"],d(f,"end"),{"transfer"});}
            for (const auto& t:r["transitions"]) {publication(t.at("start_event"),d(t,"start"),{"allocate","reconfigure"});if (!t["end_event"].is_null()) publication(t["end_event"],d(t,"end"),{"composition"});}
            for (const auto& j:r["jobs"]) for (const auto& p:j["phases"]) publication(p.at("event_index"),d(p,"time"),{"phase"});
        }
    } catch (const std::exception& e) {errors.insert(std::string("Malformed audit records: ")+e.what());}
    return {errors.begin(),errors.end()};
}
}
