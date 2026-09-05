#include "taiyin/ziwei/ziweicore.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <climits>
#include <future>

using namespace taiyin;
using namespace taiyin::ziwei;
namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void hash(uint32_t value, uint32_t& h) {
    for (int i=0; i<4; ++i) h = (h ^ ((value >> (i*8)) & 255u)) * UINT32_C(0x01000193);
}
uint32_t digest(const PlacementAnchors& a, StarId life, StarId body, const TransformSet& t,
    const std::vector<uint8_t>& positions, const std::vector<StarTransformMask>& masks) {
    uint32_t h = UINT32_C(0x811c9dc5);
    hash(to_index(a.bureau),h); hash(to_index(a.ziwei),h); hash(to_index(a.tianfu),h); hash(to_index(a.body_palace),h);
    for (Branch b : a.palace_positions) hash(to_index(b),h);
    for (Stem s : a.palace_stems) hash(to_index(s),h);
    hash(life,h); hash(body,h); hash(t.lu,h); hash(t.quan,h); hash(t.ke,h); hash(t.ji,h);
    for (uint8_t b : positions) hash(b,h);
    for (StarTransformMask m : masks) hash(m,h);
    return h;
}
uint32_t digest(const CastingChart& c) {
    const PlacementResult& p = c.plate;
    return digest(p.anchors,p.life_master,p.body_master,p.year_transformations,p.star_positions,p.transformation_masks);
}
uint32_t digest(const NatalChart& c) {
    PlacementAnchors a;
    a.bureau=c.anchors.bureau; a.ziwei=c.anchors.ziwei; a.tianfu=c.anchors.tianfu;
    a.body_palace=c.body_palace; a.palace_positions=c.anchors.palace_positions; a.palace_stems=c.palace_stems;
    std::vector<uint8_t> positions;
    check(dump_natal_star_positions(c,&positions)==TAIYIN_STATUS_OK,"dump natal");
    return digest(a,c.life_master,c.body_master,c.transformations.birth_year,positions,c.transformations.marks_by_star);
}
NatalChart natal(Gender gender, const AnchorOptions& options, const CompiledRules& rules) {
    // JS fromZonedTime(2000-01-01 12:00 UTC+8). Calendar-free fixture, not a fabricated casting birth.
    CalendarFacts f = {};
    f.birth.gender = gender;
    f.birth.virtual_time.year=2000; f.birth.virtual_time.month=1; f.birth.virtual_time.day=1; f.birth.virtual_time.hour=12;
    f.lunar_date.year=1999; f.lunar_date.historical_year=1999; f.lunar_date.month=11; f.lunar_date.day=25;
    f.effective_lunar_year=1999; f.effective_lunar_month=11; f.solar_day_from_previous_jie=26;
    f.lunar_pillars.year = Ganzhi{static_cast<Stem>(5),static_cast<Branch>(3)};
    f.lunar_pillars.month = Ganzhi{static_cast<Stem>(2),static_cast<Branch>(0)};
    f.lunar_pillars.day = Ganzhi{static_cast<Stem>(4),static_cast<Branch>(6)};
    f.lunar_pillars.hour=f.lunar_pillars.day; f.solar_term_pillars=f.lunar_pillars;
    Anchors a; Branch body;
    check(compute_anchors(f,options,&a,&body)==TAIYIN_STATUS_OK,"fixture anchors");
    NatalChart c;
    check(make_natal_chart(f,a,body,options.rules,rules,&c)==TAIYIN_STATUS_OK,"fixture natal");
    return c;
}
PlacementPatch edit(int kind) {
    PlacementPatch p;
    if(kind==1) p.year_stem=9;
    if(kind==2 || kind==3) {
        p.year_stem=9; p.year_branch=7; p.month=3; p.day=30; p.hour_branch=2;
        p.update_bureau=kind==3 ? 1 : 0;
    }
    if(kind==4) { p.year_stem=0; p.year_branch=1; }
    return p;
}
struct RandomState { int calls=0; bool reject=false; Status error=TAIYIN_STATUS_OK; };
Status random_source(void* data,uint32_t* value) {
    RandomState& s=*static_cast<RandomState*>(data); ++s.calls;
    *value=(s.reject || s.calls==1) ? UINT32_MAX : 259199u; return s.error;
}
Status throwing_source(void*,uint32_t*) { throw std::runtime_error("rng"); }
}
int main() {
    const LoadedRules loaded=load_rules_from_toml(std::string(TAIYIN_ZIWEI_TEST_ROOT)+"/rules/default.toml");
    const CompiledRules& rules=loaded.compiled;
    std::ifstream file(std::string(TAIYIN_ZIWEI_TEST_ROOT)+"/tests/fixtures/manual_casting_js.txt");
    check(file.good(),"open JS fixture");
    std::string line; int rows=0;
    while(std::getline(file,line)) {
        if(line.empty() || line[0]=='#') continue;
        ++rows; const int before=failures;
        std::istringstream row(line); std::string type; row>>type;
        if(type=="number") {
            std::string number; uint32_t index,h; row>>number>>index>>h;
            CastingChart c;
            check(casting_chart_from_number(number,Gender::Male,ZiweiChartMode::TianPan,rules,&c)==TAIYIN_STATUS_OK,"number build");
            check(c.index==index && digest(c)==h,"JS number-v1 mapping and complete plate");
        } else if(type=="casting") {
            uint32_t index,h0,h1,h2,h3; int gender,mode;
            row>>index>>gender>>mode>>h0>>h1>>h2>>h3;
            CastingChart c,d,s,e,r;
            check(casting_chart_from_index(index,static_cast<Gender>(gender),static_cast<ZiweiChartMode>(mode),rules,&c)==TAIYIN_STATUS_OK,"index build");
            PlacementPatch p=edit(2); p.update_bureau=(index/4320)%2==0 ? 1 : 0;
            check(modify_casting_chart(c,p,rules,&d)==TAIYIN_STATUS_OK,"casting modify");
            check(shift_casting_life_palace(d,-25,&s)==TAIYIN_STATUS_OK,"casting shift");
            PlacementPatch day; day.day=1;
            check(modify_casting_chart(s,day,rules,&e)==TAIYIN_STATUS_OK,"casting chained edit");
            check(reset_casting_chart(e,&r)==TAIYIN_STATUS_OK,"casting reset");
            check(digest(c)==h0 && digest(d)==h1 && digest(e)==h2 && digest(r)==h3,"JS casting anchors/stars/all four-transform masks");
        } else if(type=="natal") {
            int gender,mode,boundary,kind; uint32_t h0,h1,h2,h3;
            row>>gender>>mode>>boundary>>kind>>h0>>h1>>h2>>h3;
            AnchorOptions o=default_anchor_options(); o.chart_mode=static_cast<ZiweiChartMode>(mode);
            o.rules.wu_hu_dun_year_boundary=o.rules.sihua_year_boundary=o.rules.body_master_year_boundary=static_cast<PillarBoundary>(boundary);
            NatalChart c=natal(static_cast<Gender>(gender),o,rules),d,s,e,r;
            check(modify_natal_chart(c,edit(kind),o,rules,&d)==TAIYIN_STATUS_OK,"natal modify");
            check(shift_natal_life_palace(d,-25,&s)==TAIYIN_STATUS_OK,"natal shift");
            PlacementPatch day; day.day=1;
            check(modify_natal_chart(s,day,o,rules,&e)==TAIYIN_STATUS_OK,"natal chained edit");
            check(reset_natal_chart(e,&r)==TAIYIN_STATUS_OK,"natal reset");
            check(digest(c)==h0 && digest(d)==h1 && digest(e)==h2 && digest(r)==h3,"JS natal anchors/stars/all four-transform masks");
            check(d.birth_facts.lunar_date.year==1999 && d.life_master==c.life_master && d.body_master==c.body_master
                && d.palace_stems==c.palace_stems,"immutable birth facts and frame");
            DecadeLimit a,b;
            check(make_decade_by_index(c,1999,1,&a)==TAIYIN_STATUS_OK
                && make_decade_by_index(d,1999,1,&b)==TAIYIN_STATUS_OK,"modified decade");
            check(b.start_age==bureau_number(d.anchors.bureau) && b.start_year==1999+b.start_age-1,"bureau reschedules start age and year");
            if(kind!=3) check(a.start_year==b.start_year,"retained bureau preserves date");
        } else check(false,"unknown fixture type");
        check(!row.fail(),"parse fixture");
        if(failures!=before) std::cerr<<"fixture "<<line<<'\n';
    }
    check(rows==427,"all JS fixtures consumed");
    // Exhaustive index decoding is cheap integer arithmetic, not exhaustive chart builds.
    for(uint32_t i=0;i<kCastingSpaceSize;++i) {
        PlacementInput p; check(casting_input_from_index(i,&p)==TAIYIN_STATUS_OK,"decode");
        const uint32_t year=(6*p.year_stem-5*p.year_branch+60)%60;
        check(((year*12+p.month-1)*30+p.day-1)*12+p.hour_branch==i,"index-v1 bijection");
    }
    CastingChart c,other;
    RandomState random;
    check(random_casting_chart(Gender::Male,ZiweiChartMode::TianPan,rules,&c,random_source,&random)==TAIYIN_STATUS_OK
        && random.calls==2 && c.index==259199 && c.method==CastingMethod::Random,"reject biased tail then replayable random index");
    check(casting_chart_from_index(c.index,Gender::Male,ZiweiChartMode::TianPan,rules,&other)==TAIYIN_STATUS_OK
        && digest(other)==digest(c),"random replay");
    const uint32_t old=digest(c);
    random.calls=0; random.reject=true;
    check(random_casting_chart(Gender::Male,ZiweiChartMode::TianPan,rules,&c,random_source,&random)==TAIYIN_ERROR_INTERNAL
        && random.calls==128 && digest(c)==old,"bounded rejection and strong failure guarantee");
    random.error=TAIYIN_ERROR_UNSUPPORTED;
    check(random_casting_chart(Gender::Male,ZiweiChartMode::TianPan,rules,&c,random_source,&random)==TAIYIN_ERROR_UNSUPPORTED,"RNG error propagation");
    check(random_casting_chart(Gender::Male,ZiweiChartMode::TianPan,rules,&c,throwing_source)==TAIYIN_ERROR_INTERNAL,"RNG exceptions contained");
    random.error=1;
    check(random_casting_chart(Gender::Male,ZiweiChartMode::TianPan,rules,&c,random_source,&random)==TAIYIN_ERROR_INVALID_ARGUMENT,"positive callback status cannot masquerade as success");
    check(random_casting_chart(Gender::Female,ZiweiChartMode::RenPan,rules,&other)==TAIYIN_STATUS_OK && other.index<kCastingSpaceSize,"OS random source");
    for(const char* number : {"","-1","1.5"," 1","1e3"}) check(
        casting_chart_from_number(number,Gender::Male,ZiweiChartMode::TianPan,rules,&c)==TAIYIN_ERROR_INVALID_ARGUMENT,"invalid reported number");
    PlacementInput invalid; invalid.year_stem=INT_MAX;
    check(make_casting_chart(invalid,Gender::Male,ZiweiChartMode::TianPan,rules,&c)==TAIYIN_ERROR_INVALID_ARGUMENT,"no integer narrowing");
    PlacementPatch bad; bad.day=0;
    check(modify_casting_chart(c,bad,rules,&c)==TAIYIN_ERROR_INVALID_ARGUMENT && digest(c)==old,"invalid patch leaves output unchanged");
    CompiledRules mismatch=rules; ++mismatch.registry_fingerprint;
    check(modify_casting_chart(c,PlacementPatch(),mismatch,&c)==TAIYIN_ERROR_INVALID_ARGUMENT,"context mismatch");
    PlacementInput input; input.year_branch=1;
    check(make_casting_chart(input,Gender::Male,ZiweiChartMode::TianPan,rules,&other)==TAIYIN_STATUS_OK
        && other.plate.omitted_placements.size()==2,"independent incompatible year indices omit only void stars");
    for(const OmittedPlacement& omitted:other.plate.omitted_placements)
        check(other.plate.star_positions[omitted.star_id]==255,"omitted position sentinel");
    CompiledRules custom=rules;
    PlacementRule& rule=custom.placement.natal[0];
    rule.inputs.assign(1,RuleInputSource::SolarDayStem); rule.strides.assign(1,1); rule.table.assign(10,3);
    check(make_casting_chart(PlacementInput(),Gender::Male,ZiweiChartMode::TianPan,custom,&other)==TAIYIN_STATUS_OK
        && other.plate.star_positions[rule.star_id]==255,"no invented day Ganzhi for custom table");
    const AnchorOptions o=default_anchor_options(); NatalChart n=natal(Gender::Male,o,custom),d;
    PlacementPatch day; day.day=30;
    check(modify_natal_chart(n,day,o,custom,&d)==TAIYIN_STATUS_OK
        && d.palaces[3].stars.test(rule.star_id),"birth edit retains actual day Ganzhi");
    // When boundaries differ, empty edits preserve their respective pillars.
    n.birth_facts.solar_term_pillars.year=Ganzhi{Stem::Jia,Branch::Zi};
    n.anchors.solar_term=n.birth_facts.solar_term_pillars;
    check(make_natal_chart(n.birth_facts,n.anchors,n.body_palace,o.rules,custom,&n)==TAIYIN_STATUS_OK,"different year boundary fixture");
    check(modify_natal_chart(n,PlacementPatch(),o,custom,&d)==TAIYIN_STATUS_OK && digest(n)==digest(d),"different lunar/solar years remain unchanged");
    // Five bureau choices, including reverting to the original fixed bureau.
    for (int b=0;b<5;++b) {
        const Bureau bureau=static_cast<Bureau>(b);
        CastingChart original,edited,retained;
        check(make_casting_chart(PlacementInput(),Gender::Female,ZiweiChartMode::RenPan,rules,&original,&bureau)==TAIYIN_STATUS_OK
            && original.plate.anchors.bureau==bureau,"manual fixed bureau");
        check(modify_casting_chart(original,edit(3),rules,&edited)==TAIYIN_STATUS_OK,"fixed bureau can be recomputed");
        PlacementPatch retain; retain.update_bureau=0;
        check(modify_casting_chart(edited,retain,rules,&retained)==TAIYIN_STATUS_OK
            && retained.plate.anchors.bureau==bureau,"revert to original not previous bureau");
    }
    CastingChart survivor;
    {
        CastingChart ephemeral;
        check(casting_chart_from_number("000123456",Gender::Male,ZiweiChartMode::TianPan,rules,&ephemeral)==TAIYIN_STATUS_OK,"ephemeral source");
        check(modify_casting_chart(ephemeral,edit(3),rules,&survivor)==TAIYIN_STATUS_OK,"retain source snapshot");
    }
    check(reset_casting_chart(survivor,&survivor)==TAIYIN_STATUS_OK
        && survivor.index==209225 && survivor.number=="123456" && !survivor.original_chart,"in-place reset after original destruction");
    const uint32_t shared_digest=digest(survivor);
    std::vector<std::future<bool> > tasks;
    for (int worker=0;worker<8;++worker) tasks.push_back(std::async(std::launch::async,[&rules,&survivor,shared_digest]() {
        for(int i=0;i<16;++i) {
            CastingChart changed,restored;
            if(modify_casting_chart(survivor,edit(i%5),rules,&changed)!=TAIYIN_STATUS_OK
                || shift_casting_life_palace(changed,i-8,&changed)!=TAIYIN_STATUS_OK
                || reset_casting_chart(changed,&restored)!=TAIYIN_STATUS_OK
                || digest(restored)!=shared_digest) return false;
        }
        return true;
    }));
    for(auto& task:tasks) check(task.get(),"parallel edits share immutable source and rules");
    std::cout<<rows<<" JS fixtures checked; failures="<<failures<<'\n';
    return failures ? 1 : 0;
}
