#include "distriarch/engine.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

namespace distriarch {
namespace {
const std::vector<std::string> policies={"static","demand","cost"};
std::string csv(const Json& v) {std::string s=v.is_string()?v.get<std::string>():v.dump(),out="\"";for (char c:s) {out+=c;if (c=='"') out+='"';}return out+'"';}
}
Json experiments(const std::string& output) {
    const std::filesystem::path dir(output);std::filesystem::create_directories(dir/"traces");
    const std::vector<std::uint32_t> seeds={11,29,47};std::vector<std::tuple<std::string,std::string,Configuration>> configs;
    for (const auto& entry:scenario_catalog()) for (auto seed:seeds) {const auto name=entry["id"].get<std::string>();configs.emplace_back("main",name,scenario(name,seed));}
    for (int bandwidth:{24,90,360}) for (auto seed:seeds) {auto c=scenario("constrained",seed);c.links.back().bandwidth=bandwidth*static_cast<double>(MiB);configs.emplace_back("sensitivity","backbone-"+std::to_string(bandwidth)+"-MiBs",c);}
    for (auto seed:seeds) {auto c=scenario("expensive",seed);c.settings.idealized_zero_reconfiguration=true;configs.emplace_back("ablation","idealized-zero-reconfiguration",c);}
    Json rows=Json::array();
    for (const auto& [group,variant,base]:configs) for (const auto& policy:policies) {
        auto c=base;c.policy=policy;auto r=run(c);const auto errors=audit(r);if (!errors.empty()||r["status"]!="completed") throw std::runtime_error("Experiment failed: "+variant+"/"+policy+" "+Json(errors).dump());
        const auto filename=group+"-"+variant+"-"+std::to_string(c.seed)+"-"+policy+".json.gz";write_json((dir/"traces"/filename).string(),r,true);const auto& s=r["summary"];
        rows.push_back({{"group",group},{"scenario",c.name},{"variant",variant},{"seed",c.seed},{"policy",policy},{"status",r["status"]},{"workload_hash",r["workload_hash"]},{"hardware_hash",r["hardware_hash"]},{"makespan",s["makespan"]},{"completed_jobs",s["completed_jobs"]},{"unfinished_jobs",s["unfinished_jobs"]},{"completed_chunks",s["completed_chunks"]},{"throughput",s["throughput_jobs_per_s"]},{"bytes_transferred",s["bytes_transferred"]},{"reconfigurations",s["reconfiguration_count"]},{"reconfiguration_time",s["reconfiguration_time"]},{"reconfiguration_bytes",s["reconfiguration_bytes"]},{"manager_service_time",s["manager_service_time"]},{"max_initial_wait",s["max_initial_wait"]},{"progress_jain",s["progress_jain"]},{"backbone_utilization",s["links"]["backbone"]},{"resource_utilization",s["resources"]},{"jobs",s["jobs"]},{"trace","traces/"+filename}});
    }
    for (auto& row:rows) for (const auto& baseline:rows) if (baseline["group"]==row["group"]&&baseline["variant"]==row["variant"]&&baseline["seed"]==row["seed"]&&baseline["policy"]=="static") {row["improvement_pct"]=100*(baseline["makespan"].get<double>()-row["makespan"].get<double>())/baseline["makespan"].get<double>();break;}
    std::set<std::tuple<std::string,std::string,std::string>> groups;for (const auto& r:rows) groups.emplace(r["group"],r["variant"],r["policy"]);Json aggregate=Json::array();
    for (const auto& [group,variant,policy]:groups) {
        std::vector<double> times;double improvements=0,reconfigs=0;for (const auto& r:rows) if (r["group"]==group&&r["variant"]==variant&&r["policy"]==policy) {times.push_back(r["makespan"].get<double>());improvements+=r["improvement_pct"].get<double>();reconfigs+=r["reconfigurations"].get<double>();}
        const auto n=times.size();const double mean=std::accumulate(times.begin(),times.end(),0.0)/n;double ss=0;for (double x:times) ss+=(x-mean)*(x-mean);
        aggregate.push_back({{"group",group},{"variant",variant},{"policy",policy},{"n",n},{"mean",mean},{"minimum",*std::min_element(times.begin(),times.end())},{"maximum",*std::max_element(times.begin(),times.end())},{"sample_sd",std::sqrt(ss/(n-1))},{"mean_improvement_pct",improvements/n},{"mean_reconfigurations",reconfigs/n}});
    }
    Json payload={{"implementation","C++20"},{"version",3},{"trace_schema","event-boundaries-v1"},{"seeds",seeds},{"run_count",rows.size()},{"label","Synthetic architectural simulation; descriptive variation, not statistical significance"},{"rows",rows},{"aggregate",aggregate}};write_json((dir/"summary.json").string(),payload);
    std::vector<std::string> columns;for (auto it=rows[0].begin();it!=rows[0].end();++it) if (it.key()!="resource_utilization"&&it.key()!="jobs") columns.push_back(it.key());
    std::ofstream file(dir/"runs.csv",std::ios::binary);for (std::size_t i=0;i<columns.size();++i) file<<(i?",":"")<<csv(columns[i]);file<<'\n';for (const auto& row:rows) {for (std::size_t i=0;i<columns.size();++i) file<<(i?",":"")<<csv(row.at(columns[i]));file<<'\n';}if (!file) throw std::runtime_error("CSV write failed");plot_results(payload,output);return payload;
}
void plot_results(const Json& payload,const std::string& output) {
    std::map<std::string,std::string> colors={{"static","#72839b"},{"demand","#e5a24b"},{"cost","#23b9a3"}};double maximum=0;for (const auto& a:payload["aggregate"]) if (a["group"]=="main") maximum=std::max(maximum,a["maximum"].get<double>());const double scale=360/maximum;
    std::ofstream f(std::filesystem::path(output)/"main-comparison.svg");f<<std::fixed<<std::setprecision(3)<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1100\" height=\"590\" viewBox=\"0 0 1100 590\"><rect width=\"1100\" height=\"590\" fill=\"#101b2d\"/><g font-family=\"Arial,sans-serif\" fill=\"#e3ecfa\"><text x=\"45\" y=\"44\" font-size=\"24\">DistriArch | C++20 Main experiment</text><text x=\"45\" y=\"72\" fill=\"#97acc8\" font-size=\"14\">Synthetic seconds; mean and min/max over seeds 11, 29, 47. Lower is faster.</text>";
    for (int tick=0;tick<=50;tick+=10) {const double y=465-tick*scale;if (y>=95) f<<"<path d=\"M55 "<<y<<"H1060\" stroke=\"#29374c\"/><text x=\"20\" y=\""<<y+5<<"\" font-size=\"12\">"<<tick<<"</text>";}
    int i=0;for (const auto& entry:scenario_catalog()) {const auto name=entry["id"].get<std::string>();const double x=90+i++*200;int j=0;for (const auto& policy:policies) {for (const auto& a:payload["aggregate"]) if (a["group"]=="main"&&a["variant"]==name&&a["policy"]==policy) {const double h=a["mean"].get<double>()*scale,bx=x+j*45;f<<"<rect x=\""<<bx<<"\" y=\""<<465-h<<"\" width=\"32\" height=\""<<h<<"\" rx=\"3\" fill=\""<<colors[policy]<<"\"/><path d=\"M"<<bx+16<<" "<<465-a["maximum"].get<double>()*scale<<"V"<<465-a["minimum"].get<double>()*scale<<"\" stroke=\"white\" stroke-width=\"2\"/><text x=\""<<bx+16<<"\" y=\""<<455-h<<"\" text-anchor=\"middle\" font-size=\"12\">"<<std::setprecision(2)<<a["mean"].get<double>()<<std::setprecision(3)<<"</text>";}++j;}f<<"<text x=\""<<x+58<<"\" y=\"495\" text-anchor=\"middle\" font-size=\"14\">"<<name<<"</text>";}
    i=0;for (const auto& p:policies) {f<<"<rect x=\""<<300+i*180<<"\" y=\"535\" width=\"14\" height=\"14\" rx=\"3\" fill=\""<<colors[p]<<"\"/><text x=\""<<323+i*180<<"\" y=\"547\" font-size=\"14\">"<<p<<"</text>";++i;}f<<"</g></svg>\n";if (!f) throw std::runtime_error("SVG write failed");
}
}
