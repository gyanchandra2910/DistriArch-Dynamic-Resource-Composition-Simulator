#include "distriarch/model.hpp"
#include "picosha2.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

namespace distriarch {
namespace {
void fields(const Json& j,std::initializer_list<const char*> allowed,const std::string& path) {
    if (!j.is_object()) throw std::invalid_argument(path+": expected an object");
    std::set<std::string> names;
    for (auto key:allowed) names.insert(key);
    for (auto it=j.begin();it!=j.end();++it)
        if (!names.contains(it.key())) throw std::invalid_argument(path+": unknown field "+it.key());
}
std::string text_field(const Json& j,const std::string& key,const std::string& path,
                       std::optional<std::string> fallback={},std::size_t maximum=200) {
    if (!j.contains(key)) {
        if (fallback) return *fallback;
        throw std::invalid_argument(path+"."+key+": required field");
    }
    const auto& v=j.at(key);
    if (!v.is_string() || v.get_ref<const std::string&>().size()>maximum || v.get_ref<const std::string&>().empty())
        throw std::invalid_argument(path+"."+key+": expected a nonempty bounded string");
    return v.get<std::string>();
}
double number_field(const Json& j,const std::string& key,const std::string& path,
                    double low,double high,std::optional<double> fallback={}) {
    if (!j.contains(key)) {
        if (fallback) return *fallback;
        throw std::invalid_argument(path+"."+key+": required field");
    }
    const auto& v=j.at(key);
    if (!v.is_number()) throw std::invalid_argument(path+"."+key+": expected a number");
    const double d=v.get<double>();
    if (!std::isfinite(d) || d<low || d>high) throw std::invalid_argument(path+"."+key+": number outside allowed range");
    return d;
}
Bytes integer_field(const Json& j,const std::string& key,const std::string& path,
                    Bytes low,Bytes high,std::optional<Bytes> fallback={}) {
    if (!j.contains(key)) {
        if (fallback) return *fallback;
        throw std::invalid_argument(path+"."+key+": required integer");
    }
    if (!j.at(key).is_number_integer()) throw std::invalid_argument(path+"."+key+": expected an integer");
    const double d=number_field(j,key,path,static_cast<double>(low),static_cast<double>(high));
    return static_cast<Bytes>(d);
}
const Json& array_field(const Json& j,const std::string& key,std::size_t minimum,std::size_t maximum,const std::string& path) {
    if (!j.contains(key) || !j.at(key).is_array() || j.at(key).size()<minimum || j.at(key).size()>maximum)
        throw std::invalid_argument(path+"."+key+": invalid array size");
    return j.at(key);
}
void id_check(const std::string& id) {
    static const std::regex pattern("^[A-Za-z0-9_-]{1,40}$");
    if (!std::regex_match(id,pattern)) throw std::invalid_argument("Invalid endpoint ID: "+id);
}

// MT19937 init_by_array + the 53-bit two-word real transform. This reproduces
// the existing integer-seeded workload fixtures without calling Python.
class SeededRandom {
    std::array<std::uint32_t,624> state_{};
    std::size_t index_=624;
    std::uint32_t next() {
        if (index_==624) {
            for (std::size_t i=0;i<624;++i) {
                const auto y=(state_[i]&0x80000000U)|(state_[(i+1)%624]&0x7fffffffU);
                state_[i]=state_[(i+397)%624]^(y>>1)^((y&1U)?0x9908b0dfU:0U);
            }
            index_=0;
        }
        auto y=state_[index_++];
        y^=y>>11; y^=(y<<7)&0x9d2c5680U; y^=(y<<15)&0xefc60000U; y^=y>>18;
        return y;
    }
public:
    explicit SeededRandom(std::uint32_t seed) {
        state_[0]=19650218U;
        for (std::size_t i=1;i<624;++i) state_[i]=1812433253U*(state_[i-1]^(state_[i-1]>>30))+static_cast<std::uint32_t>(i);
        std::size_t i=1;
        for (int k=0;k<624;++k) {
            state_[i]=(state_[i]^((state_[i-1]^(state_[i-1]>>30))*1664525U))+seed;
            if (++i==624) { state_[0]=state_[623];i=1; }
        }
        for (int k=0;k<623;++k) {
            state_[i]=(state_[i]^((state_[i-1]^(state_[i-1]>>30))*1566083941U))-static_cast<std::uint32_t>(i);
            if (++i==624) { state_[0]=state_[623];i=1; }
        }
        state_[0]=0x80000000U;
    }
    double unit() {
        const auto a=next()>>5;
        const auto b=next()>>6;
        return (a*67108864.0+b)/9007199254740992.0;
    }
};
}

void to_json(Json& j,const Resource& r) { j={{"id",r.id},{"kind",r.kind},{"capacity",r.capacity},{"rate",r.rate},{"available_at",r.available_at},{"capability",r.capability}}; }
void to_json(Json& j,const Link& l) { j={{"id",l.id},{"a",l.a},{"b",l.b},{"bandwidth",l.bandwidth},{"latency",l.latency}}; }
void to_json(Json& j,const Phase& p) { j={{"name",p.name},{"chunks",p.chunks},{"cpu",p.cpu},{"memory",p.memory},{"read",p.read},{"write",p.write},{"accelerator",p.accelerator},{"acc_input",p.acc_input},{"acc_output",p.acc_output},{"working_set",p.working_set},{"storage_size",p.storage_size},{"capability",p.capability}}; }
void to_json(Json& j,const Workload& w) { j={{"id",w.id},{"name",w.name},{"arrival",w.arrival},{"phases",w.phases}}; }
void to_json(Json& j,const Settings& s) { j={{"interval",s.interval},{"decision",s.decision},{"registration",s.registration},{"control_bytes",s.control_bytes},{"preparation",s.preparation},{"cooldown",s.cooldown},{"minimum_benefit",s.minimum_benefit},{"overhead_margin",s.overhead_margin},{"idealized_zero_reconfiguration",s.idealized_zero_reconfiguration}}; }
void to_json(Json& j,const Configuration& c) { j={{"name",c.name},{"description",c.description},{"seed",c.seed},{"policy",c.policy},{"resources",c.resources},{"links",c.links},{"workloads",c.workloads},{"settings",c.settings},{"horizon",c.horizon},{"max_events",c.max_events}}; }
void to_json(Json& j,const Composition& c) { j={{"compute",c.compute},{"cores",c.cores},{"memory",c.memory},{"memory_bytes",c.memory_bytes},{"storage",c.storage},{"storage_bytes",c.storage_bytes},{"accelerator",c.accelerator?Json(*c.accelerator):Json(nullptr)}}; }
std::vector<std::pair<std::string,Bytes>> Composition::holdings() const {
    std::vector<std::pair<std::string,Bytes>> h={{compute,cores},{memory,memory_bytes},{storage,storage_bytes}};
    if (accelerator) h.emplace_back(*accelerator,1);
    return h;
}
std::string Composition::tie_key() const {
    return "Composition(compute='"+compute+"', cores="+std::to_string(cores)+", memory='"+memory+"', memory_bytes="+std::to_string(memory_bytes)+", storage='"+storage+"', storage_bytes="+std::to_string(storage_bytes)+", accelerator="+(accelerator?"'"+*accelerator+"'":"None")+")";
}
Observation Job::observation() const { const auto& p=spec.phases.at(phase_index);return {spec.id,p,p.chunks-chunk_index,composition,last_change,waiting_since}; }

Configuration parse_config(const Json& j) {
    fields(j,{"name","description","seed","policy","resources","links","workloads","settings","horizon","max_events"},"config");
    Configuration c;
    c.name=text_field(j,"name","config");
    if (j.contains("description")) { if (!j["description"].is_string()) throw std::invalid_argument("description must be text");c.description=j["description"].get<std::string>(); }
    c.seed=static_cast<std::uint32_t>(integer_field(j,"seed","config",0,4294967295LL,11));
    c.policy=text_field(j,"policy","config","cost");
    if (c.policy!="static" && c.policy!="demand" && c.policy!="cost") throw std::invalid_argument("Unknown policy");
    c.horizon=number_field(j,"horizon","config",std::numeric_limits<double>::min(),10000,240);
    c.max_events=static_cast<std::uint64_t>(integer_field(j,"max_events","config",100,500000,100000));
    std::set<std::string> ids,kinds,link_ids,job_ids;
    for (const auto& r:array_field(j,"resources",4,24,"config")) {
        fields(r,{"id","kind","capacity","rate","available_at","capability"},"resource");
        Resource v;
        v.id=text_field(r,"id","resource",{},40);id_check(v.id);
        v.kind=text_field(r,"kind","resource");
        if (v.id=="manager" || !ids.insert(v.id).second) throw std::invalid_argument("Duplicate or reserved resource ID");
        if (v.kind!="compute" && v.kind!="memory" && v.kind!="storage" && v.kind!="accelerator") throw std::invalid_argument("Unknown resource kind");
        kinds.insert(v.kind);
        v.capacity=integer_field(r,"capacity","resource",1,1LL<<50);
        v.rate=number_field(r,"rate","resource",std::numeric_limits<double>::min(),1e15);
        v.available_at=number_field(r,"available_at","resource",0,10000,0);
        v.capability=text_field(r,"capability","resource","general");
        if ((v.kind=="compute" && v.capacity>64)||(v.kind=="accelerator"&&v.capacity!=1)) throw std::invalid_argument("Invalid core/device capacity");
        c.resources.push_back(v);
    }
    if (kinds.size()!=4) throw std::invalid_argument("One of each resource kind is required");
    std::map<std::string,std::set<std::string>> graph;
    for (const auto& l:array_field(j,"links",1,60,"config")) {
        fields(l,{"id","a","b","bandwidth","latency"},"link");
        Link v;
        v.id=text_field(l,"id","link",{},40);v.a=text_field(l,"a","link",{},40);v.b=text_field(l,"b","link",{},40);
        id_check(v.id);id_check(v.a);id_check(v.b);
        if (v.a==v.b || ids.contains(v.id) || v.id=="manager" || !link_ids.insert(v.id).second) throw std::invalid_argument("Invalid/duplicate link ID or self link");
        v.bandwidth=number_field(l,"bandwidth","link",std::numeric_limits<double>::min(),1e15);
        v.latency=number_field(l,"latency","link",0,100);
        graph[v.a].insert(v.b);graph[v.b].insert(v.a);c.links.push_back(v);
    }
    std::set<std::string> seen;std::vector<std::string> todo={"manager"};
    while (!todo.empty()) { auto node=todo.back();todo.pop_back();if (seen.insert(node).second) for (const auto& next:graph[node]) if (!seen.contains(next)) todo.push_back(next); }
    for (const auto& id:ids) if (!seen.contains(id)) throw std::invalid_argument("Resource has no path to manager: "+id);
    for (const auto& w:array_field(j,"workloads",1,16,"config")) {
        fields(w,{"id","name","arrival","phases"},"workload");
        Workload v;
        v.id=text_field(w,"id","workload",{},40);v.name=text_field(w,"name","workload");
        if (!job_ids.insert(v.id).second) throw std::invalid_argument("Duplicate workload ID");
        v.arrival=number_field(w,"arrival","workload",0,10000);
        for (const auto& p:array_field(w,"phases",1,12,"workload")) {
            fields(p,{"name","chunks","cpu","memory","read","write","accelerator","acc_input","acc_output","working_set","storage_size","capability"},"phase");
            Phase f;
            f.name=text_field(p,"name","phase",{},60);f.chunks=static_cast<int>(integer_field(p,"chunks","phase",1,100));
            f.cpu=number_field(p,"cpu","phase",0,1e9);f.accelerator=number_field(p,"accelerator","phase",0,1e9,0);
            f.memory=integer_field(p,"memory","phase",0,1LL<<40);f.read=integer_field(p,"read","phase",0,1LL<<40);
            f.write=integer_field(p,"write","phase",0,1LL<<40,0);f.acc_input=integer_field(p,"acc_input","phase",0,1LL<<40,0);f.acc_output=integer_field(p,"acc_output","phase",0,1LL<<40,0);
            f.working_set=integer_field(p,"working_set","phase",1,1LL<<40);f.storage_size=integer_field(p,"storage_size","phase",1,1LL<<40);f.capability=text_field(p,"capability","phase","tensor");
            v.phases.push_back(f);
        }
        c.workloads.push_back(v);
    }
    if (j.contains("settings")) {
        const auto& s=j["settings"];
        fields(s,{"interval","decision","registration","control_bytes","preparation","cooldown","minimum_benefit","overhead_margin","idealized_zero_reconfiguration"},"settings");
        auto& v=c.settings;
        v.interval=number_field(s,"interval","settings",.05,100,.8);v.decision=number_field(s,"decision","settings",0,10,.004);v.registration=number_field(s,"registration","settings",0,10,.002);
        v.control_bytes=integer_field(s,"control_bytes","settings",1,MiB,512);v.preparation=number_field(s,"preparation","settings",0,100,.035);v.cooldown=number_field(s,"cooldown","settings",0,100,1.5);
        v.minimum_benefit=number_field(s,"minimum_benefit","settings",0,100,.04);v.overhead_margin=number_field(s,"overhead_margin","settings",1,10,1.2);
        if (s.contains("idealized_zero_reconfiguration")) { if (!s["idealized_zero_reconfiguration"].is_boolean()) throw std::invalid_argument("Idealization flag must be boolean");v.idealized_zero_reconfiguration=s["idealized_zero_reconfiguration"].get<bool>(); }
    }
    return c;
}

Json scenario_catalog() {
    return Json::array({{{"id","steady"},{"name","Steady state"},{"description","A stable compute stream: adaptation has little useful work to do."}},
        {{"id","phases"},{"name","Shifting bottlenecks"},{"description","One application moves from CPU to memory, storage and tensor work."}},
        {{"id","concurrent"},{"name","Competing applications"},{"description","Unequal concurrent demands, release, reuse and a late accelerator."}},
        {{"id","constrained"},{"name","Constrained fabric"},{"description","A narrow shared backbone makes remote data placement expensive."}},
        {{"id","expensive"},{"name","The cost of change"},{"description","Short alternating phases and expensive live-state migration."}}});
}
Configuration scenario(const std::string& name,std::uint32_t seed,const std::string& policy) {
    Configuration c;c.name=name;c.seed=seed;c.policy=policy;
    bool found=false;
    for (const auto& entry:scenario_catalog()) if (entry["id"]==name) { c.description=entry["description"].get<std::string>();found=true; }
    if (!found) throw std::invalid_argument("Unknown scenario: "+name);
    c.resources={{"cpu-a","compute",4,70,0,"general"},{"cpu-b","compute",8,150,0,"general"},
        {"mem-a","memory",512*MiB,1200.0*MiB,0,"general"},{"mem-b","memory",768*MiB,280.0*MiB,0,"general"},
        {"store-a","storage",4096*MiB,90.0*MiB,0,"general"},{"store-b","storage",8192*MiB,650.0*MiB,0,"general"},
        {"acc-a","accelerator",1,1000,0,"tensor"},{"acc-b","accelerator",1,1800,2.5,"tensor"}};
    for (const auto& r:c.resources) c.links.push_back({"edge-"+r.id,r.id,r.id.back()=='a'?"rack-a":"rack-b",500.0*MiB,.0008});
    c.links.push_back({"management","manager","rack-a",500.0*MiB,.0008});
    c.links.push_back({"backbone","rack-a","rack-b",(name=="constrained"?24.0:name=="expensive"?60.0:180.0)*MiB,.003});
    SeededRandom rng(seed);
    auto phase=[&](const std::string& kind,int chunks,bool large=false) {
        Phase p;p.name=kind;p.chunks=chunks;p.cpu=8;p.memory=2*MiB;p.read=MiB;p.write=MiB;p.working_set=(large?192:48)*MiB;p.storage_size=(large?192:64)*MiB;
        if (kind=="Compute") p.cpu=420;
        else if (kind=="Memory") { p.memory=96*MiB;p.cpu=2;p.read=0;p.write=0; }
        else if (kind=="Storage") { p.read=64*MiB;p.write=16*MiB;p.cpu=3; }
        else if (kind=="Tensor") { p.accelerator=500;p.acc_input=8*MiB;p.acc_output=2*MiB;p.cpu=12; }
        p.cpu=std::nearbyint(p.cpu*(.9+(1.1-.9)*rng.unit())*1e6)/1e6;
        return p;
    };
    if (name=="steady") c.workloads.push_back({"app-1","Vector analysis",.05,{phase("Compute",12)}});
    else if (name=="phases"||name=="constrained") { Workload w{"app-1","Adaptive pipeline",.05,{}};for (const auto& k:{"Compute","Memory","Storage","Tensor"}) w.phases.push_back(phase(k,5));c.workloads.push_back(w); }
    else if (name=="concurrent") {
        c.workloads.push_back({"app-1","Model training",.05,{phase("Compute",5),phase("Tensor",6)}});
        c.workloads.push_back({"app-2","Memory analytics",.05,{phase("Memory",8),phase("Compute",3)}});
        c.workloads.push_back({"app-3","Data ingestion",.4,{phase("Storage",5)}});
        c.workloads.push_back({"app-4","Tensor inference",1.8,{phase("Tensor",7)}});
    } else { Workload w{"app-1","Short phase pipeline",.05,{}};for (const auto& k:{"Compute","Memory","Compute","Tensor","Storage","Memory"}) w.phases.push_back(phase(k,2,true));c.workloads.push_back(w);c.settings.preparation=.7; }
    return parse_config(Json(c));
}

std::string digest(const Json& j) { return picosha2::hash256_hex_string(j.dump(-1,' ',true)).substr(0,16); }
Json read_json(const std::string& path) {
    std::string data;
    constexpr std::size_t limit=256*1024*1024;
    if (std::filesystem::path(path).extension()==".gz") {
        gzFile f=gzopen(path.c_str(),"rb");if (!f) throw std::runtime_error("Cannot open "+path);
        std::array<char,65536> buffer{};int n=0;
        while ((n=gzread(f,buffer.data(),static_cast<unsigned>(buffer.size())))>0) { data.append(buffer.data(),static_cast<std::size_t>(n));if (data.size()>limit) { gzclose(f);throw std::runtime_error("JSON exceeds 256 MiB limit"); } }
        if (n<0) { gzclose(f);throw std::runtime_error("Invalid gzip stream: "+path); }gzclose(f);
    } else {
        std::ifstream file(path,std::ios::binary);if (!file) throw std::runtime_error("Cannot open "+path);
        if (std::filesystem::file_size(path)>limit) throw std::runtime_error("JSON exceeds 256 MiB limit");
        data.assign(std::istreambuf_iterator<char>(file),{});
    }
    return Json::parse(data);
}
void write_json(const std::string& path,const Json& value,bool gzip) {
    const auto parent=std::filesystem::path(path).parent_path();if (!parent.empty()) std::filesystem::create_directories(parent);
    const auto text=value.dump(gzip?-1:2);
    if (gzip) {
        gzFile f=gzopen(path.c_str(),"wb6");if (!f) throw std::runtime_error("Cannot write "+path);
        const int written=gzwrite(f,text.data(),static_cast<unsigned>(text.size()));
        const int closed=gzclose(f);if (written!=static_cast<int>(text.size())||closed!=Z_OK) throw std::runtime_error("Failed writing gzip "+path);
    } else { std::ofstream file(path,std::ios::binary);if (!file) throw std::runtime_error("Cannot write "+path);file<<text<<'\n';if (!file) throw std::runtime_error("Failed writing "+path); }
}
}
