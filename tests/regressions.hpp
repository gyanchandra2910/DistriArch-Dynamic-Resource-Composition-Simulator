// Independent regression fixtures for the four defects reviewed after v2.0.
// Included after the native harness helpers so these are part of --all.
Configuration shrink_fixture(const std::string& pool,const std::string& policy) {
    auto c=small();c.name="shrink-"+pool;c.policy=policy;c.resources[0].capacity=8;
    c.resources[1].capacity=(pool=="memory"?100:500)*MiB;c.resources[1].rate=1000.0*MiB;
    c.resources[2].capacity=(pool=="storage"?100:500)*MiB;c.resources[2].rate=1000.0*MiB;
    for (auto& l:c.links) {l.bandwidth=1000.0*MiB;l.latency=.0001;}
    Phase first;first.name="large";first.chunks=1;first.cpu=800;first.working_set=(pool=="memory"?80:20)*MiB;first.storage_size=(pool=="storage"?80:20)*MiB;
    auto smaller=first;smaller.name="small";smaller.cpu=4000;smaller.working_set=20*MiB;smaller.storage_size=20*MiB;
    auto waiting=smaller;waiting.name="waiting";waiting.cpu=100;if (pool=="memory") waiting.working_set=50*MiB;else waiting.storage_size=50*MiB;
    c.workloads={{"A","Shrinking owner",0,{first,smaller}},{"B","Waiting request",2,{waiting}}};return c;
}
Configuration cost_fixture() {
    auto c=small();for (auto& l:c.links) {l.bandwidth=1000000;l.latency=0;}
    c.resources.push_back({"m-new","memory",1000,100});c.links.push_back({"l-new","m-new","switch",1000000,0});return c;
}
void register_regressions() {
    for (const std::string policy:{"demand","cost"}) for (const std::string pool:{"memory","storage"})
        add("regressions","reclaim "+pool+" for waiting app / "+policy,[pool,policy]{
            const auto r=run(shrink_fixture(pool,policy));check(r["status"]=="completed");check(audit(r).empty());
            const auto& a=r["jobs"][0];const auto& b=r["jobs"][1];
            check(b["first_start"].get<double>()<a["finished"].get<double>(),"B must start using reclaimed capacity before A finishes");
            bool shrunk=false;const auto id=pool=="memory"?"m":"s";
            for (const auto& x:r["ownership"]) if (x["app"]=="A"&&x["resource"]==id&&x["amount"]==20*MiB&&x["time"].get<double>()<a["finished"].get<double>()) shrunk=true;
            check(shrunk,"A must publish its smaller reservation before completion");
            check(a["completed_chunks"]==2&&b["completed_chunks"]==1);
            bool charged=false;for (const auto& t:r["transitions"]) if (t["app"]=="A"&&t["reconfiguration"].get<bool>()) {charged|=t["end"].get<double>()>t["start"].get<double>();}
            check(charged,"reclamation must pay actual transition/control cost");
        });
    add("regressions","static keeps its initial reservations when phase shrinks",[]{auto r=run(shrink_fixture("memory","static"));check(r["jobs"][1]["first_start"].get<double>()>=r["jobs"][0]["finished"].get<double>());check(audit(r).empty());});
    add("regressions","release unused cores at a safe boundary",[]{auto c=small();c.policy="cost";c.workloads[0].phases[0].cpu=0;Simulator s(c);discover(s);place(s,{"c",4,"m",100,"s",200,{}});auto [target,d]=s.controller.choose(s.jobs[0].observation());check(target&&target->cores==1,"idle phase must not retain four unused cores");check(d["accepted"]==true);});
    add("regressions","release unused accelerator without moving live pools",[]{auto c=small();c.policy="cost";Simulator s(c);discover(s);place(s,{"c",4,"m",100,"s",200,"a"});auto [target,d]=s.controller.choose(s.jobs[0].observation());check(target&&!target->accelerator,"non-accelerator phase must release the unused device");check(target->memory=="m"&&target->storage=="s");check(d["accepted"]==true);});
    // Each message crosses two zero-latency links: round trip = 4*10/1e6.
    // A move charges source read + two serialized hops + destination write.
    struct CostCase {const char* name;Bytes bytes;bool migrate;double moves;};
    const std::vector<CostCase> cases={
        {"same-pool growth",200,false,100.0/50+2*100.0/1000000+100.0/100},
        {"migration without growth",100,true,100.0/100+2*100.0/1000000+100.0/100},
        {"migration plus growth",200,true,(100.0/100+2*100.0/1000000+100.0/100)+(100.0/50+2*100.0/1000000+100.0/100)},
        {"same-pool shrink",50,false,0},
        {"migration plus shrink",50,true,50.0/100+2*50.0/1000000+50.0/100}};
    for (const auto& x:cases) add("regressions",std::string("complete queue-free cost / ")+x.name,[x]{Simulator s(cost_fixture());Composition old{"c",1,"m",100,"s",200,{}};auto target=old;target.memory_bytes=x.bytes;if (x.migrate) target.memory="m-new";const double expected=.007+4*10.0/1000000+x.moves;close(s.controller.overhead(old,target),expected);});
    for (const auto& x:cases) add("regressions",std::string("executed plan matches analytic body / ")+x.name,[x]{
        auto cfg=cost_fixture();cfg.workloads[0].phases[0].working_set=x.bytes;
        Simulator s(cfg);discover(s);Composition old{"c",1,"m",100,"s",200,{}};auto target=old;target.memory_bytes=x.bytes;if (x.migrate) target.memory="m-new";place(s,old);
        const auto plan=s.transition_plan(old,target);s.transition(s.jobs[0],target);
        while (s.transitions[0]["end"].is_null()) s.clock.step();
        std::vector<Json> reads,writes;Bytes payload=0;
        for (const auto& i:s.intervals) {if (i["operation"]=="reconfiguration state read") reads.push_back(i);if (i["operation"]=="reconfiguration state write") writes.push_back(i);}
        for (const auto& f:s.network.transfers) if (f["purpose"]=="reconfiguration"&&!f["control"].get<bool>()) payload+=f["bytes"].get<Bytes>();
        check(reads.size()==plan.moves.size()&&writes.size()==plan.moves.size());Bytes planned=0;
        for (std::size_t n=0;n<plan.moves.size();++n) {const auto& m=plan.moves[n];planned+=m.bytes;check(reads[n]["resource"]==m.source&&writes[n]["resource"]==m.destination);close(reads[n]["end"].get<double>()-reads[n]["start"].get<double>(),static_cast<double>(m.bytes)/s.resources.at(m.source).rate);close(writes[n]["end"].get<double>()-writes[n]["start"].get<double>(),static_cast<double>(m.bytes)/s.resources.at(m.destination).rate);}
        check(payload==planned);
        if (!reads.empty()) close(s.transitions[0]["end"].get<double>()-reads.front()["start"].get<double>(),x.moves);
        close(s.controller.overhead(old,target),cfg.settings.decision+4*10.0/1000000+x.moves);
    });
    add("regressions","storage migration and preparation are in the shared plan",[]{auto c=cost_fixture();c.resources.push_back({"s-new","storage",2000,25});c.links.push_back({"l-store-new","s-new","switch",1000000,0});Simulator s(c);Composition old{"c",1,"m",100,"s",200,{}};auto target=old;target.storage="s-new";target.accelerator="a";const auto plan=s.transition_plan(old,target);check(plan.moves.size()==1&&plan.moves[0].bytes==200&&plan.prepare_accelerator=="a");close(s.controller.overhead(old,target),.007+4*10.0/1000000+200.0/50+2*200.0/1000000+200.0/25+.05);});
    add("regressions","idealized transition estimate remains explicitly zero",[]{auto c=cost_fixture();c.settings.idealized_zero_reconfiguration=true;Simulator s(c);Composition old{"c",1,"m",100,"s",200,{}};auto target=old;target.memory="m-new";target.memory_bytes=200;close(s.controller.overhead(old,target),0);});
    add("regressions","composition record publishes at its exact event boundary",[]{auto r=run(scenario());for (const auto& c:r["compositions"]) {check(c.contains("event_index"),"composition needs authoritative event_index");const auto index=c["event_index"].get<std::size_t>();check(r["events"][index]["kind"]=="composition");check(r["events"][index]["app"]==c["app"]);check(r["events"][index]["time"]==c["time"]);}});
    add("regressions","ownership and discovery publish at exact event boundaries",[]{auto r=run(scenario());for (const auto& x:r["ownership"]) {check(x.contains("event_index"),"ownership needs authoritative event_index");const auto kind=r["events"].at(x["event_index"].get<std::size_t>())["kind"];check(kind=="allocate"||kind=="reconfigure"||kind=="composition"||kind=="release");}check(r.contains("discovery_events"),"discovery needs event indices");for (auto it=r["discovery_events"].begin();it!=r["discovery_events"].end();++it) {const auto& e=r["events"].at(it.value().get<std::size_t>());check(e["kind"]=="discovery");check(e["resource"]==it.key());}});
    add("regressions","audit rejects a same-time publication shifted before commit",[]{auto r=run(scenario());auto& c=r["compositions"][0];c["event_index"]=c["event_index"].get<std::size_t>()-1;const auto errors=audit(r);check(std::find(errors.begin(),errors.end(),"Event publication does not match its record")!=errors.end());});
    add("regressions","audit rejects discovery published at its preceding service event",[]{auto r=run(scenario());auto& index=r["discovery_events"]["cpu-a"];index=index.get<std::size_t>()-1;check(!audit(r).empty());});
}
