#include "engine/RenderGraph.h"
#include <array>
#include <iostream>
#include <random>

namespace {
using Graph=engine::RenderGraph;
using A=Graph::Access;
using F=Graph::PassFlags;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Exception,class Function> void rejects(Function fn,const char* message) {
    bool rejected=false;try{fn();}catch(const Exception&){rejected=true;}check(rejected,message);
}
const Graph::Lifetime& lifetime(const Graph::Plan& plan,const std::string& name) {
    for(const auto& life:plan.lifetimes)if(life.resource==name)return life;
    throw std::runtime_error("Missing resource lifetime: "+name);
}
void versionsAndExecution() {
    Graph graph;graph.enableCulling();graph.exportResource("result");
    std::vector<int> recorded;std::vector<std::string> labels;
    graph.add("old",{{"scratch",A::Write}},[&]{recorded.push_back(0);});
    graph.add("unused-reader",{{"scratch",A::Read},{"unused",A::Write}},[&]{recorded.push_back(1);});
    graph.add("replacement",{{"scratch",A::Write}},[&]{recorded.push_back(2);});
    graph.add("result",{{"scratch",A::Read},{"result",A::Write}},[&]{recorded.push_back(3);});
    const auto plan=graph.execute([&](const auto& name){labels.push_back(name);});
    check(recorded==std::vector<int>({2,3}) && labels==std::vector<std::string>({"replacement","result"}),
          "WAW / WAR incorrectly kept an overwritten value or invoked a dead callback");
    check(plan.culledPasses==std::vector<size_t>({0,1}) && plan.producers[3]==std::set<size_t>({2}),"Wrong data-flow producer");
    check(plan.dependencies[2].empty() && plan.dependencies[3]==std::set<size_t>({2}),"Hazards retained dead passes");
    check(plan.lifetimes.size()==2 && lifetime(plan,"scratch").first==2,"Dead resources retained allocations");
    graph.enableCulling(false);recorded.clear();graph.execute();
    check(recorded==std::vector<int>({0,1,2,3}),"Disabling culling failed to restore all passes");
}
void sideEffectsAndReadWrite() {
    Graph graph;graph.enableCulling();graph.import("history");graph.exportResource("history");
    std::vector<int> recorded;
    graph.add("producer",{{"work",A::Write}},[&]{recorded.push_back(0);});
    graph.add("unused",{{"trash",A::Write}},[&]{recorded.push_back(1);});
    graph.add("history",{{"history",A::ReadWrite},{"work",A::Read}},[&]{recorded.push_back(2);});
    graph.add("readback",{{"work",A::Read}},[&]{recorded.push_back(3);},F::NeverCull);
    graph.add("notification",{},[&]{recorded.push_back(4);},F::NeverCull);
    check(graph.execute().activePasses==std::vector<size_t>({0,2,3,4}),"Export or side effect lost its input closure");
    check(recorded==std::vector<int>({0,2,3,4}),"Side effect did not execute in order");
    Graph rw;rw.enableCulling();rw.exportResource("result");
    rw.add("initialize",{{"result",A::Write}},[]{});
    rw.add("blend",{{"result",A::ReadWrite}},[]{});
    check(rw.compile().activePasses==std::vector<size_t>({0,1}),"ReadWrite dropped its previous producer");
    Graph importOnly;importOnly.enableCulling();importOnly.import("texture");importOnly.exportResource("texture");
    check(importOnly.compile().activePasses.empty(),"Imported output invented a producer");
    Graph noRoots;noRoots.enableCulling();noRoots.add("pure",{{"unused",A::Write}},[]{});
    check(noRoots.compile().culledPasses==std::vector<size_t>({0}),"Rootless opt-in graph was not culled");
    Graph importedWrite;importedWrite.enableCulling();importedWrite.import("history");
    importedWrite.add("discarded-history",{{"history",A::Write}},[]{});
    check(importedWrite.compile().activePasses.empty(),"Import unexpectedly created an external write root");
}
void subresourcesAndHazards() {
    Graph graph;graph.enableCulling();graph.describe("image",{2,2});
    graph.exportResource("image",{1,1,1,1});
    graph.add("mip0",{{"image",A::Write,{0,1,0,2}}},[]{});
    graph.add("layer0",{{"image",A::Write,{1,1,0,1}}},[]{});
    graph.add("layer1-old",{{"image",A::Write,{1,1,1,1}}},[]{});
    graph.add("layer1-current",{{"image",A::Write,{1,1,1,1}}},[]{});
    check(graph.compile().activePasses==std::vector<size_t>({3}),"Export crossed a mip/layer or retained an old version");
    graph.exportResource("image",{0,1,0,0});
    check(graph.compile().activePasses==std::vector<size_t>({0,3}),"Remaining-layer export failed");

    // With an intermediate writer removed, the retained read and write must
    // still be ordered (WAR), and retained writers must keep their WAW edge.
    Graph hazards;hazards.enableCulling();hazards.import("shared");hazards.exportResource("shared");
    hazards.add("read",{{"shared",A::Read}},[]{},F::NeverCull);
    hazards.add("first-write",{{"shared",A::Write}},[]{},F::NeverCull);
    hazards.add("dead-write",{{"shared",A::Write}},[]{});
    hazards.add("last-write",{{"shared",A::Write}},[]{});
    const auto plan=hazards.compile();
    check(plan.dependencies[1]==std::set<size_t>({0}) && plan.dependencies[3]==std::set<size_t>({1}),
          "Removing an intermediate writer lost a retained WAR / WAW hazard");
    check(plan.producers[3].empty(),"Pure write incorrectly depends on the previous value");
}
void lifetimesAndExports() {
    Graph graph;graph.enableCulling();
    for(auto name:{"a","b","dead","output"})graph.describe(name,{1,1,true,"rgba16"});
    graph.add("a",{{"a",A::Write}},[]{});
    graph.add("consume-a",{{"a",A::Read}},[]{},F::NeverCull);
    graph.add("dead",{{"dead",A::Write}},[]{});
    graph.add("output",{{"output",A::Write}},[]{});graph.exportResource("output");
    graph.add("b",{{"b",A::Write}},[]{},F::NeverCull);
    const auto plan=graph.compile();
    check(plan.lifetimes.size()==3,"Culled temporary still has a lifetime / allocation");
    check(lifetime(plan,"a").aliasSlot==lifetime(plan,"output").aliasSlot,"Disjoint live textures missed reuse");
    check(lifetime(plan,"output").last==5 && lifetime(plan,"b").aliasSlot!=lifetime(plan,"output").aliasSlot,
          "Exported transient storage was reused before external consumption");
}
void validationAndPlans() {
    Graph invalid;invalid.enableCulling();int recorded=0;
    invalid.add("dead-invalid",{{"missing",A::Read}},[&]{++recorded;});
    rejects<std::logic_error>([&]{invalid.execute();},"Culling hid an uninitialized read");
    check(recorded==0,"Invalid graph recorded before validation");
    Graph missing;missing.enableCulling();missing.exportResource("missing");
    rejects<std::logic_error>([&]{missing.compile();},"Uninitialized export accepted");
    Graph partial;partial.describe("image",{2,1});partial.import("image",{0,1,0,1});partial.exportResource("image");
    rejects<std::logic_error>([&]{partial.compile();},"Partial import initialized unexported mip");
    Graph bounds;bounds.describe("image",{1,1});bounds.exportResource("image",{1,1,0,1});
    rejects<std::out_of_range>([&]{bounds.compile();},"Invalid export subresource accepted");
    Graph feedback;feedback.enableCulling();feedback.import("image");
    feedback.add("dead-feedback",{{"image",A::Read},{"image",A::Write}},[]{});
    rejects<std::logic_error>([&]{feedback.compile();},"Culling hid overlapping feedback");
    Graph valid;valid.add("effect",{},[&]{++recorded;},F::NeverCull);
    auto plan=valid.compile();valid.execute(plan);check(recorded==1,"Compiled plan did not execute");
    valid.add("new-effect",{},[]{},F::NeverCull);
    rejects<std::logic_error>([&]{valid.execute(plan);},"Stale compiled plan accepted");
    Graph other;rejects<std::logic_error>([&]{other.execute(plan);},"Foreign compiled plan accepted");
}
void randomizedEquivalence() {
    // Independent scalar reference executes all passes. Check output values and
    // observable readback values against the same operations with graph culling.
    struct Op {int input,output;bool accumulate,effect;uint64_t value;};
    std::mt19937 random(7919);size_t removed=0;
    for(int trial=0;trial<400;++trial) {
        std::vector<Op> operations;
        for(int i=0;i<48;++i) {
            int input=int(random()%8),output=int(random()%8);
            if(input==output)input=-1;
            operations.push_back({input,output,bool(random()%2),random()%13==0,uint64_t(random())});
        }
        std::array<uint64_t,8> reference{},actual{};std::vector<uint64_t> expectedEffects,effects;
        auto apply=[](const Op& op,auto& values,auto& observations) {
            if(op.effect)observations.push_back(op.input<0?0:values[op.input]);
            values[op.output]=(op.accumulate?values[op.output]:0)+(op.input<0?0:values[op.input])*17+op.value;
        };
        for(const auto& op:operations)apply(op,reference,expectedEffects);
        Graph graph;graph.enableCulling();
        for(int i=0;i<8;++i)graph.import(std::to_string(i));
        graph.exportResource("0");graph.exportResource("7");
        for(size_t i=0;i<operations.size();++i) {
            const auto op=operations[i];std::vector<Graph::Use> uses;
            if(op.input>=0)uses.push_back({std::to_string(op.input),A::Read});
            uses.push_back({std::to_string(op.output),op.accumulate?A::ReadWrite:A::Write});
            graph.add(std::to_string(i),uses,[&,op]{apply(op,actual,effects);},op.effect?F::NeverCull:F::None);
        }
        const auto plan=graph.execute();removed+=plan.culledPasses.size();
        check(reference[0]==actual[0] && reference[7]==actual[7] && expectedEffects==effects,
              "Randomized culling changed an exported value or observable side effect");
        for(auto i:plan.activePasses)for(auto dep:plan.dependencies[i])
            check(dep<i && std::find(plan.activePasses.begin(),plan.activePasses.end(),dep)!=plan.activePasses.end(),
                  "Active plan has a forward or dead hazard dependency");
    }
    check(removed>1000,"Randomized graphs did not exercise significant dead branches");
    std::cout<<"Randomized reference equivalence: 400 graphs, "<<removed<<" dead passes removed\n";
}
}
int main() {
    try {versionsAndExecution();sideEffectsAndReadWrite();subresourcesAndHazards();lifetimesAndExports();validationAndPlans();randomizedEquivalence();
        std::cout<<"Render graph culling tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
