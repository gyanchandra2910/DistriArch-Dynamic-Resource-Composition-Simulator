#include "distriarch/engine.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace distriarch;
namespace {
struct Test {std::string group,name;std::function<void()> body;};std::vector<Test> tests;
void check(bool v,const std::string& message="assertion failed") {if (!v) throw std::runtime_error(message);}
void close(double actual,double expected) {check(std::abs(actual-expected)<=1e-9*std::max(1.0,std::abs(expected)),"expected "+std::to_string(expected)+", got "+std::to_string(actual));}
template<class F> void rejects(F f) {bool threw=false;try {f();}catch(const std::exception&){threw=true;}check(threw,"expected rejection");}
void drain(Clock& c) {while (!c.empty()) c.step();}
Configuration small() {
    Configuration c;c.name="analytic";c.horizon=100;c.resources={{"c","compute",4,100},{"m","memory",1000,100},{"s","storage",2000,50},{"a","accelerator",1,200,0,"tensor"}};
    for (const auto& id:{"c","m","s","a","manager"}) c.links.push_back({std::string("l-")+id,id,"switch",1000,.01});
    Phase p;p.name="one";p.chunks=1;p.cpu=200;p.working_set=100;p.storage_size=200;c.workloads={{"j","Analytic job",0,{p}}};c.settings.control_bytes=10;c.settings.registration=.005;c.settings.decision=.007;c.settings.preparation=.05;return parse_config(Json(c));
}
void discover(Simulator& s) {for (const auto& [id,r]:s.resources) s.discovered.insert(id);}
void place(Simulator& s,const Composition& c) {auto& j=s.jobs[0];j.composition=c;for (const auto& [id,n]:c.holdings()) s.own(id,j.spec.id,n);}
void add(const std::string& group,const std::string& name,std::function<void()> body) {tests.push_back({group,name,std::move(body)});}
#include "regressions.hpp"
void register_tests() {
    register_regressions();
    add("model","CPU duration and core lanes",[]{Simulator s(small());double end=-1;s.service("c",200.0/(100*2),"j","cpu",[&]{end=s.clock.now;},2);drain(s.clock);close(end,1);check(s.intervals[0]["amount"]==2);});
    add("model","two-hop transfer and byte-hops",[]{Simulator s(small());double end=-1;s.network.send("c","m",100,"j","test",[&]{end=s.clock.now;});drain(s.clock);close(end,.22);Bytes bytes=0;for (const auto& i:s.intervals) bytes+=i["bytes"].get<Bytes>();check(bytes==200);});
    add("model","shared link FIFO contention",[]{Simulator s(small());std::vector<double> ends;for (int i=0;i<2;++i) s.network.send("c","m",100,"j","test",[&]{ends.push_back(s.clock.now);});drain(s.clock);close(ends[0],.22);close(ends[1],.33);double waits=0;for (const auto& i:s.intervals) waits+=i["start"].get<double>()-i["requested"].get<double>();close(waits,.11);});
    add("model","storage service queue",[]{Simulator s(small());std::vector<double> ends;for (int i=0;i<2;++i) s.service("s",2,"j","read",[&]{ends.push_back(s.clock.now);});drain(s.clock);check(ends==std::vector<double>({2,4}));});
    add("model","ownership discovery capacity release reuse",[]{Simulator s(small());rejects([&]{s.own("m","first",1000);});s.discovered.insert("m");s.own("m","first",1000);rejects([&]{s.own("m","second",1);});s.own("m","first",0);s.own("m","second",1000);check(s.owners["m"].size()==1);rejects([&]{s.own("m","second",-1);});});
    add("model","accelerator transfer execution and release",[]{auto c=small();auto& p=c.workloads[0].phases[0];p.cpu=0;p.accelerator=400;p.acc_input=100;p.acc_output=50;Simulator s(c);discover(s);place(s,{"c",1,"m",100,"s",200,"a"});s.start_chunk(s.jobs[0]);drain(s.clock);close(*s.jobs[0].finished,2.34);close(*s.jobs[0].released,2.385);});
    add("model","registration network and manager cost",[]{Simulator s(small());s.register_resource(s.resources.at("c"));drain(s.clock);close(s.discovery_times["c"].get<double>(),.045);check(s.intervals.back()["kind"]=="manager");});
    add("model","telemetry and decision cost",[]{Simulator s(small());discover(s);place(s,{"c",4,"m",100,"s",200,{}});s.jobs[0].status="boundary";s.request_decision(s.jobs[0]);while (std::none_of(s.events.begin(),s.events.end(),[](const auto& e){return e["kind"]=="decision";})) s.clock.step();for (const auto& e:s.events) if (e["kind"]=="decision") close(e["time"].get<double>(),.047);});
    add("model","equal costs retain unused storage",[]{auto c=small();c.policy="demand";c.resources.push_back({"a-store","storage",2000,50});c.links.push_back({"l-alt","a-store","switch",1000,.01});Simulator s(c);discover(s);place(s,{"c",4,"m",100,"s",200,{}});auto [target,decision]=s.controller.choose(s.jobs[0].observation());check(target==s.jobs[0].composition);check(decision["accepted"]==false);});
    add("model","live memory retained until migration commit",[]{auto c=small();c.resources.push_back({"m-new","memory",1000,100});c.links.push_back({"l-m-new","m-new","switch",1000,.01});Simulator s(c);discover(s);Composition old{"c",1,"m",100,"s",200,{}};auto target=old;target.memory="m-new";place(s,old);s.transition(s.jobs[0],target);check(s.owners["m"]["j"]==100&&s.owners["m-new"]["j"]==100);while (s.transitions[0]["end"].is_null()) {s.clock.step();if (s.transitions[0]["end"].is_null()) check(s.owners["m"]["j"]==100);}double start=0;for (const auto& i:s.intervals) if (i["operation"]=="reconfiguration state read") start=i["start"].get<double>();close(s.transitions[0]["end"].get<double>()-start,2.22);for (const auto& x:s.ledger) if (x["resource"]=="m"&&x["amount"]==0) check(x["time"]==s.transitions[0]["end"]);});
    add("model","stable simultaneous insertion order",[]{Clock c;std::vector<int> order;c.at(1,[&]{order.push_back(1);c.at(1,[&]{order.push_back(3);});});c.at(1,[&]{order.push_back(2);});drain(c);check(order==std::vector<int>({1,2,3}));rejects([&]{c.at(0,[]{});});rejects([&]{c.at(std::numeric_limits<double>::infinity(),[]{});});});
    std::vector<std::pair<std::string,std::function<void(Json&)>>> mutations={
        {"zero service rate",[](Json& c){c["resources"][0]["rate"]=0;}},
        {"nonfinite rate",[](Json& c){c["resources"][0]["rate"]=std::numeric_limits<double>::quiet_NaN();}},
        {"negative link bandwidth",[](Json& c){c["links"][0]["bandwidth"]=-1;}},
        {"unknown field",[](Json& c){c["typo"]=true;}},
        {"duplicate ID",[](Json& c){c["resources"][1]["id"]="c";}},
        {"disconnected resource",[](Json& c){c["links"][0]["a"]="disconnected";}},
        {"zero chunks",[](Json& c){c["workloads"][0]["phases"][0]["chunks"]=0;}},
        {"fractional chunks",[](Json& c){c["workloads"][0]["phases"][0]["chunks"]=1.5;}},
        {"negative seed",[](Json& c){c["seed"]=-1;}},
        {"invalid policy",[](Json& c){c["policy"]="magic";}},
        {"invalid ablation flag",[](Json& c){c["settings"]["idealized_zero_reconfiguration"]=1;}}};
    for (const auto& [name,mutation]:mutations) add("model","reject "+name,[mutation]{Json c=small();mutation(c);rejects([&]{parse_config(c);});});
    add("model","infeasible capacity explicitly stalls",[]{auto c=small();c.workloads[0].phases[0].working_set=1001;const auto r=run(c);check(r["status"]=="stalled");check(r["summary"]["unfinished_jobs"]==1);check(r["summary"]["completed_chunks"]==0);check(r["compositions"].empty());check(audit(r).empty());});
    add("model","live capacity deadlock reported",[]{auto c=small();c.resources[0].capacity=8;c.resources[1].capacity=100;auto& w=c.workloads[0];w.phases[0].working_set=50;auto p=w.phases[0];p.working_set=100;w.phases.push_back(p);auto second=w;second.id="j2";c.workloads.push_back(second);auto r=run(c);check(r["status"]=="stalled");check(r["summary"]["completed_chunks"]==2);check(r["elapsed"].get<double>()<c.horizon);check(audit(r).empty());});
    add("model","horizon clips unfinished transfers",[]{auto c=small();c.horizon=.02;auto r=run(c);check(r["status"]=="timeout");close(r["elapsed"].get<double>(),.02);for (const auto& x:r["summary"]["links"]) check(x.get<double>()>=0&&x.get<double>()<=1);check(audit(r).empty());});
    add("model","utilization denominators by hand",[]{Simulator s(small());discover(s);s.own("m","j",500);s.service("c",1,"j","cpu",[]{},2);s.clock.at(2,[&]{s.own("m","j",0);});drain(s.clock);auto summary=s.result()["summary"];close(summary["resources"]["c"]["service_utilization"].get<double>(),.25);close(summary["resources"]["m"]["reservation_utilization"].get<double>(),.5);check(summary["completed_jobs"]==0);});
    for (const std::string policy:{"demand","cost"}) add("model","growth moves bytes under "+policy,[policy]{auto c=small();c.policy=policy;auto p=c.workloads[0].phases[0];p.working_set=200;p.name="larger state";c.workloads[0].phases.push_back(p);auto r=run(c);check(r["status"]=="completed");check(r["summary"]["completed_chunks"]==2);close(r["summary"]["logical_work"]["cpu"]["completed"].get<double>(),400);Bytes bytes=0;for (const auto& f:r["transfers"]) if (f["purpose"]=="reconfiguration"&&!f["control"].get<bool>()) bytes+=f["bytes"].get<Bytes>();check(bytes==100);check(audit(r).empty());});
    add("model","static larger phase fails honestly",[]{auto c=small();c.policy="static";auto p=c.workloads[0].phases[0];p.working_set=200;c.workloads[0].phases.push_back(p);auto r=run(c);check(r["status"]=="infeasible");check(r["jobs"][0]["released"].is_number());check(r["summary"]["completed_chunks"]==1);check(audit(r).empty());});
    add("model","zero-work chunks are bounded events",[]{auto c=small();c.workloads[0].phases[0].cpu=0;c.workloads[0].phases[0].chunks=100;auto r=run(c);check(r["status"]=="completed");check(r["summary"]["completed_chunks"]==100);check(audit(r).empty());});
    add("model","invalid low-level demands rejected",[]{Simulator s(small());rejects([&]{s.network.send("c","m",-1,"j","bad",[]{});});rejects([&]{s.service("c",-1,"j","bad",[]{});});rejects([&]{s.service("c",1,"j","bad",[]{},5);});});
    for (const auto& entry:scenario_catalog()) for (const std::string policy:{"static","demand","cost"}) {
        const auto name=entry["id"].get<std::string>();add("integrity",name+"/"+policy+" completes and conserves",[name,policy]{auto r=run(scenario(name,29,policy));check(r["status"]=="completed");check(r["summary"]["completed_chunks"]==r["summary"]["total_chunks"]);check(audit(r).empty());std::map<std::string,Bytes> last;for (const auto& x:r["ownership"]) last[x["resource"].get<std::string>()]=x["used"].get<Bytes>();for (const auto& [id,n]:last) check(n==0);});
    }
    add("integrity","exact deterministic replay and input immutability",[]{auto c=scenario("concurrent",47);Json before=c;check(run(c)==run(c));check(Json(c)==before);});
    add("integrity","policies share work hardware and chunk count",[]{std::set<std::string> work,hardware;std::set<int> chunks;for (const auto* p:{"static","demand","cost"}) {auto r=run(scenario("phases",11,p));work.insert(r["workload_hash"]);hardware.insert(r["hardware_hash"]);chunks.insert(r["summary"]["completed_chunks"].get<int>());}check(work.size()==1&&hardware.size()==1&&chunks.size()==1);});
    add("integrity","useful changes and uneconomic rejection",[]{auto r=run(scenario("phases"));check(r["summary"]["reconfiguration_count"].get<int>()>0);r=run(scenario("expensive"));int rejected=0;for (const auto& e:r["events"]) if (e["kind"]=="decision"&&e["reason"].get<std::string>().starts_with("Expected benefit")) {++rejected;check(e["benefit"].get<double>()<=e["estimated_overhead"].get<double>()*1.2+.04);}check(rejected>0);});
    add("integrity","future phases and arrivals do not leak",[]{auto c=scenario("phases");c.horizon=2;auto changed=c;changed.workloads[0].phases[1].cpu=999999;changed.workloads[0].phases[1].working_set=1LL<<39;auto future=changed.workloads[0];future.id="future";future.arrival=50;changed.workloads.push_back(future);auto a=run(c),b=run(changed);check(a["events"]==b["events"]);check(a["ownership"]==b["ownership"]);check(a["compositions"]==b["compositions"]);});
    add("integrity","late discovery and endpoint reuse",[]{auto r=run(scenario("concurrent"));check(r["discovery"]["acc-b"].get<double>()>2.5);bool late=false;for (const auto& c:r["compositions"]) late|=c["composition"]["accelerator"]=="acc-b";check(late);std::map<std::string,std::set<std::string>> apps;for (const auto& x:r["ownership"]) {check(x["time"].get<double>()>=r["discovery"][x["resource"].get<std::string>()].get<double>());if (x["amount"].get<Bytes>()>0) apps[x["resource"]].insert(x["app"]);}check(std::any_of(apps.begin(),apps.end(),[](const auto& x){return x.second.size()>1;}));});
    add("integrity","JSON and gzip round trip",[]{auto r=run(scenario("phases",47));std::filesystem::create_directories("tmp/native-tests");write_json("tmp/native-tests/trace.json",r);write_json("tmp/native-tests/trace.json.gz",r,true);check(read_json("tmp/native-tests/trace.json")==r);check(read_json("tmp/native-tests/trace.json.gz")==r);check(parse_config(Json(scenario())) .name=="phases");});
    add("integrity","corrupt summary is detected",[]{auto r=run(scenario());r["summary"]["bytes_transferred"]=r["summary"]["bytes_transferred"].get<Bytes>()+1;check(audit(r)==std::vector<std::string>{"Summary does not reconcile with audit records"});});
    add("integrity","corrupt ownership is detected",[]{auto r=run(scenario());r["ownership"][0]["used"]=-100;check(!audit(r).empty());});
    add("integrity","explicit event budget",[]{auto c=scenario("steady");c.max_events=100;auto r=run(c);check(r["status"]=="event_limit");check(r["processed_events"]==100);check(r["summary"]["unfinished_jobs"]==1);check(audit(r).empty());});
    add("integrity","81 saved runs reconcile with matched baselines",[]{const auto payload=read_json("results/summary.json");check(payload["implementation"]=="C++20");check(payload["run_count"]==81);check(payload["seeds"]==Json::array({11,29,47}));int main_count=0;for (const auto& row:payload["rows"]) {main_count+=row["group"]=="main";auto trace=read_json((std::filesystem::path("results")/row["trace"].get<std::string>()).string());check(trace["implementation"]=="C++20");check(audit(trace).empty());for (const auto& base:payload["rows"]) if (base["group"]==row["group"]&&base["variant"]==row["variant"]&&base["seed"]==row["seed"]&&base["policy"]=="static") {check(row["workload_hash"]==base["workload_hash"]);check(row["hardware_hash"]==base["hardware_hash"]);close(row["improvement_pct"].get<double>(),100*(base["makespan"].get<double>()-row["makespan"].get<double>())/base["makespan"].get<double>());}}check(main_count==45);});
    add("integrity","ablation changes only explicit setting",[]{auto data=read_json("results/summary.json");for (const auto& ideal:data["rows"]) if (ideal["group"]=="ablation") for (const auto& real:data["rows"]) if (real["group"]=="main"&&real["variant"]=="expensive"&&real["seed"]==ideal["seed"]&&real["policy"]==ideal["policy"]) {auto a=read_json((std::filesystem::path("results")/real["trace"].get<std::string>()).string())["config"],b=read_json((std::filesystem::path("results")/ideal["trace"].get<std::string>()).string())["config"];a["settings"]["idealized_zero_reconfiguration"]=true;check(a==b);}});
}
}
int main(int argc,char** argv) {
    register_tests();const std::string filter=argc>1?argv[1]:"--all";if (filter!="--all"&&filter!="--model"&&filter!="--integrity"&&filter!="--regressions") {std::cerr<<"Use --model, --integrity, --regressions or --all\n";return 2;}int passed=0,failed=0;
    for (const auto& t:tests) if (filter=="--all"||filter=="--"+t.group) {try {t.body();++passed;std::cout<<"[PASS] "<<t.group<<" / "<<t.name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"[FAIL] "<<t.group<<" / "<<t.name<<": "<<e.what()<<'\n';}}
    std::cout<<"Native C++20 tests: "<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}

