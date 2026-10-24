#include "distriarch/engine.hpp"
#include "httplib.h"
#include <filesystem>
#include <iostream>
#include <semaphore>

namespace distriarch {
namespace {
void json_response(httplib::Response& response,const Json& value,int status=200) {response.status=status;response.set_content(value.dump(),"application/json; charset=utf-8");response.set_header("Cache-Control","no-store");}
struct Slot {std::counting_semaphore<2>& slots;~Slot(){slots.release();}};
}
int serve(const std::string& root,int port) {
    httplib::Server server;std::counting_semaphore<2> slots(2);
    server.new_task_queue=[] {return new httplib::ThreadPool(4);};server.set_payload_max_length(4*1024*1024);server.set_read_timeout(10);server.set_write_timeout(60);
    server.set_exception_handler([](const httplib::Request&,httplib::Response& response,std::exception_ptr ep){try {std::rethrow_exception(ep);}catch(const std::exception& e){json_response(response,{{"detail",e.what()}},500);}});
    server.set_error_handler([](const httplib::Request&,httplib::Response& response){if (response.body.empty()) json_response(response,{{"detail",response.status==404?"Not found":"HTTP request rejected"}},response.status);});
    server.Get("/api/health",[](const auto&,auto& res){json_response(res,{{"status","ok"},{"model","synthetic discrete-event architectural simulation"},{"implementation","C++20"},{"version","3.0.0"},{"trace_schema","event-boundaries-v1"}});});
    server.Get("/api/scenarios",[](const auto&,auto& res){json_response(res,scenario_catalog());});
    server.Get(R"(/api/config/([A-Za-z0-9_-]+))",[](const httplib::Request& req,httplib::Response& res){
        const auto name=req.matches[1].str();bool known=false;for (const auto& s:scenario_catalog()) if (s["id"]==name) known=true;if (!known) {json_response(res,{{"detail","Unknown scenario"}},404);return;}
        try {std::uint32_t seed=11;if (req.has_param("seed")) {const auto text=req.get_param_value("seed");if (text.empty()||text.find_first_not_of("0123456789")!=std::string::npos) throw std::invalid_argument("seed must be an unsigned 32-bit integer");const auto value=std::stoull(text);if (value>4294967295ULL) throw std::invalid_argument("seed out of range");seed=static_cast<std::uint32_t>(value);}json_response(res,scenario(name,seed));}
        catch(const std::exception& e){json_response(res,{{"detail",e.what()}},422);}
    });
    auto handler=[&slots](const httplib::Request& req,httplib::Response& res) {
        Configuration c;try {c=parse_config(Json::parse(req.body));}catch(const std::exception& e){json_response(res,{{"detail",e.what()}},422);return;}
        if (req.path=="/api/validate") {json_response(res,c);return;}
        if (!slots.try_acquire()) {json_response(res,{{"detail","Two simulations are already running; retry shortly"}},429);return;}Slot held{slots};
        if (req.path=="/api/run") {json_response(res,run(c));return;}
        Json results=Json::array();for (const auto& policy:{"static","demand","cost"}) {c.policy=policy;auto r=run(c);results.push_back({{"policy",policy},{"status",r["status"]},{"summary",r["summary"]},{"workload_hash",r["workload_hash"]},{"hardware_hash",r["hardware_hash"]}});}json_response(res,results);
    };
    server.Post("/api/validate",handler);server.Post("/api/run",handler);server.Post("/api/compare",handler);
    const auto directory=std::filesystem::absolute(root);
    server.Get("/api/experiments",[directory](const auto&,auto& res){const auto path=directory/"results"/"summary.json";json_response(res,std::filesystem::exists(path)?read_json(path.string()):Json{{"rows",Json::array()},{"aggregate",Json::array()},{"run_count",0},{"seeds",Json::array()}});});
    const auto dist=directory/"frontend"/"dist";if (std::filesystem::exists(dist/"index.html")) {if (!server.set_mount_point("/",dist.string())) throw std::runtime_error("Cannot mount frontend/dist");}
    std::cout<<"DistriArch C++20 server: http://127.0.0.1:"<<port<<"\nWorkspace: "<<directory.string()<<std::endl;
    if (!server.listen("127.0.0.1",port)) {std::cerr<<"Cannot bind localhost port "<<port<<". Choose --port or stop the previous server.\n";return 1;}return 0;
}
}
