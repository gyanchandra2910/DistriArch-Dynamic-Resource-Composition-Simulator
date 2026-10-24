#include "distriarch/engine.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace distriarch;
// Numeric JSON values have the same mathematical meaning across serializers.
bool equivalent(const Json& a,const Json& b,double& largest,const std::string& path,std::string& first) {
    if (a.is_number()&&b.is_number()) {const double x=a.get<double>(),y=b.get<double>(),delta=std::abs(x-y);largest=std::max(largest,delta);if (delta<=1e-9*std::max({1.0,std::abs(x),std::abs(y)})) return true;}
    else if (a.type()==b.type()) {
        if (a.is_object()&&a.size()==b.size()) {for (auto it=a.begin();it!=a.end();++it) {if (!b.contains(it.key())) {if (first.empty()) first=path+"/"+it.key();return false;}if (!equivalent(it.value(),b.at(it.key()),largest,path+"/"+it.key(),first)) return false;}return true;}
        else if (a.is_array()&&a.size()==b.size()) {for (std::size_t i=0;i<a.size();++i) if (!equivalent(a[i],b[i],largest,path+"/"+std::to_string(i),first)) return false;return true;}
        else if (a==b) return true;
    }
    if (first.empty()) first=path;return false;
}
Json verify_reference(const std::string& directory) {
    const std::filesystem::path dir(directory);const auto summary=read_json((dir/"summary.json").string());int passed=0;double largest=0;Json failures=Json::array();
    for (const auto& row:summary["rows"]) {
        const auto expected=read_json((dir/row["trace"].get<std::string>()).string());const auto c=parse_config(expected["config"]);auto actual=run(c);std::string mismatch;
        // Config generation is also checked independently of the saved input.
        auto generated=scenario(c.name,c.seed,c.policy);if (row["group"]=="sensitivity") generated.links.back().bandwidth=c.links.back().bandwidth;if (row["group"]=="ablation") generated.settings.idealized_zero_reconfiguration=true;
        bool ok=equivalent(Json(generated),expected["config"],largest,"config",mismatch);
        for (const auto* field:{"status","elapsed","processed_events","events","intervals","transfers","ownership","compositions","transitions","discovery","jobs","summary"}) ok=equivalent(actual[field],expected[field],largest,field,mismatch)&&ok;
        if (ok&&audit(actual).empty()) ++passed;else failures.push_back({{"trace",row["trace"]},{"first_mismatch",mismatch}});
    }
    return {{"implementation","C++20"},{"reference_runs",summary["run_count"]},{"passed",passed},{"maximum_numeric_delta",largest},{"relative_tolerance",1e-9},{"failures",failures},{"checked","Generated configurations, all event/service/transfer records, ownership, transitions, jobs and summaries; serialization-only version/hash fields excluded"}};
}
void help() {std::cout<<"DistriArch - native C++20 architectural simulator\n"
    "  distriarch run [--scenario phases] [--policy cost] [--seed 11] [--config file.json] [--output trace.json.gz]\n"
    "  distriarch config [--scenario phases] [--seed 11] --output config.json\n"
    "  distriarch experiments [--output results]\n"
    "  distriarch audit trace.json[.gz]\n"
    "  distriarch verify-reference --directory saved-results [--output report.json]\n"
    "  distriarch serve [--root .] [--port 8000]\n";}
}
int main(int argc,char** argv) {
    try {
        if (argc<2||std::string(argv[1])=="--help") {help();return argc<2?1:0;}
        const std::string command=argv[1];std::map<std::string,std::string> options;std::string positional;
        for (int i=2;i<argc;++i) {std::string key=argv[i];if (key.starts_with("--")) {if (i+1==argc||options.contains(key)) throw std::invalid_argument("Missing/duplicate option: "+key);options[key]=argv[++i];}else if (positional.empty()) positional=key;else throw std::invalid_argument("Unexpected argument: "+key);}
        const std::map<std::string,std::set<std::string>> allowed={{"run",{"--scenario","--policy","--seed","--config","--output"}},{"config",{"--scenario","--policy","--seed","--config","--output"}},{"experiments",{"--output"}},{"audit",{}},{"serve",{"--root","--port"}},{"verify-reference",{"--directory","--output"}}};
        if (!allowed.contains(command)) throw std::invalid_argument("Unknown command: "+command);for (const auto& [key,value]:options) if (!allowed.at(command).contains(key)) throw std::invalid_argument("Unknown option: "+key);
        auto opt=[&](const std::string& key,const std::string& fallback){return options.contains(key)?options.at(key):fallback;};
        auto unsigned_option=[&](const std::string& key,std::uint64_t fallback,std::uint64_t maximum){const auto s=opt(key,std::to_string(fallback));if (s.empty()||s.find_first_not_of("0123456789")!=std::string::npos) throw std::invalid_argument("Invalid integer: "+key);const auto n=std::stoull(s);if (n>maximum) throw std::invalid_argument("Integer out of range: "+key);return n;};
        if (command=="serve") {const int port=static_cast<int>(unsigned_option("--port",8000,65535));if (!port) throw std::invalid_argument("port must be positive");return serve(opt("--root","."),port);}
        if (command=="experiments") {auto result=experiments(opt("--output","results"));std::cout<<Json{{"implementation","C++20"},{"runs",result["run_count"]},{"output",opt("--output","results")}}.dump(2)<<'\n';return 0;}
        if (command=="audit") {if (positional.empty()) throw std::invalid_argument("audit requires a trace path");const auto errors=audit(read_json(positional));std::cout<<Json{{"errors",errors}}.dump(2)<<'\n';return errors.empty()?0:1;}
        if (command=="verify-reference") {if (!options.contains("--directory")) throw std::invalid_argument("--directory is required");auto report=verify_reference(options.at("--directory"));if (options.contains("--output")) write_json(options.at("--output"),report);std::cout<<report.dump(2)<<'\n';return report["failures"].empty()?0:1;}
        auto c=options.contains("--config")?parse_config(read_json(options.at("--config"))):scenario(opt("--scenario","phases"),static_cast<std::uint32_t>(unsigned_option("--seed",11,4294967295ULL)),opt("--policy","cost"));
        if (command=="config") {if (!options.contains("--output")) throw std::invalid_argument("config requires --output");write_json(options.at("--output"),c);return 0;}
        auto r=run(c);if (options.contains("--output")) {const auto path=options.at("--output");write_json(path,r,std::filesystem::path(path).extension()==".gz");}const auto& s=r["summary"];const auto errors=audit(r);
        std::cout<<Json{{"implementation","C++20"},{"status",r["status"]},{"policy",c.policy},{"scenario",c.name},{"makespan",s["makespan"]},{"completed",s["completed_jobs"]},{"unfinished",s["unfinished_jobs"]},{"reconfigurations",s["reconfiguration_count"]},{"audit_errors",errors}}.dump(2)<<'\n';return r["status"]=="completed"&&errors.empty()?0:1;
    } catch (const std::exception& e) {std::cerr<<"Error: "<<e.what()<<'\n';return 2;}
}
