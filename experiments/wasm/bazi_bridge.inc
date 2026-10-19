#include "taiyin/c/bazi.h"
#include <cmath>
#include <emscripten.h>
EM_JS(int32_t, web_bazi_rule_js, (int token,int year,int month,int day,int hour,int target,int kind,int gender), {
    return Module['taiyinBaziPredicate'](token,year,month,day,hour,target,kind,gender);
});
namespace {
int32_t web_bazi_rule(const taiyin_ganzhi_four_pillars* pillars,taiyin_ganzhi target,
                      int32_t kind,int32_t gender,void* token) {
    return web_bazi_rule_js(static_cast<int>(reinterpret_cast<uintptr_t>(token)),
        pillars->year,pillars->month,pillars->day,pillars->hour,target,kind,gender);
}
std::string bazi_text(EclipseReader& in) {
    const size_t count=web_integer<uint32_t>(in.next());
    if(count>1048576 || count>in.count-in.cursor)throw std::invalid_argument("invalid BaZi text length");
    std::string value;value.reserve(count);
    for(size_t i=0;i<count;++i){
        const auto byte=web_integer<uint8_t>(in.next());
        if(!byte)throw std::invalid_argument("embedded NUL in transported string");
        value.push_back(static_cast<char>(byte));
    }
    return value;
}
void eclipse_pack(const taiyin_calendar_datetime& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.year));
out.push_back(static_cast<double>(v.month));
out.push_back(static_cast<double>(v.day));
out.push_back(static_cast<double>(v.hour));
out.push_back(static_cast<double>(v.minute));
out.push_back(static_cast<double>(v.second));
}
void eclipse_read(taiyin_calendar_datetime& v,EclipseReader& in) { taiyin_calendar_datetime_init(&v);
v.year=web_integer<int32_t>(in.next());
v.month=web_integer<int32_t>(in.next());
v.day=web_integer<int32_t>(in.next());
v.hour=web_integer<int32_t>(in.next());
v.minute=web_integer<int32_t>(in.next());
v.second=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_ganzhi_four_pillars& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.year));
out.push_back(static_cast<double>(v.month));
out.push_back(static_cast<double>(v.day));
out.push_back(static_cast<double>(v.hour));
}
void eclipse_read(taiyin_ganzhi_four_pillars& v,EclipseReader& in) { taiyin_ganzhi_four_pillars_init(&v);
v.year=web_integer<taiyin_ganzhi>(in.next());
v.month=web_integer<taiyin_ganzhi>(in.next());
v.day=web_integer<taiyin_ganzhi>(in.next());
v.hour=web_integer<taiyin_ganzhi>(in.next());
}
void eclipse_pack(const taiyin_bazi_context_config& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.earth_palace_mode));
out.push_back(static_cast<double>(v.qiyun_direction_mode));
out.push_back(static_cast<double>(v.qiyun_time_model));
out.push_back(static_cast<double>(v.dayun_boundary_model));
}
void eclipse_read(taiyin_bazi_context_config& v,EclipseReader& in) { taiyin_bazi_context_config_init(&v);
v.earth_palace_mode=web_integer<int32_t>(in.next());
v.qiyun_direction_mode=web_integer<int32_t>(in.next());
v.qiyun_time_model=web_integer<int32_t>(in.next());
v.dayun_boundary_model=web_integer<int32_t>(in.next());
}
void eclipse_pack(const taiyin_bazi_chart& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.year_pillar));
out.push_back(static_cast<double>(v.month_pillar));
out.push_back(static_cast<double>(v.day_pillar));
out.push_back(static_cast<double>(v.hour_pillar));
out.push_back(static_cast<double>(v.ming_gong));
out.push_back(static_cast<double>(v.shen_gong));
out.push_back(static_cast<double>(v.tai_yuan));
out.push_back(static_cast<double>(v.tai_xi));
for(size_t i0=0;i0<4;++i0) out.push_back(static_cast<double>(v.hidden_stem_count[i0]));
for(size_t i0=0;i0<4;++i0) for(size_t i1=0;i1<3;++i1) out.push_back(static_cast<double>(v.hidden_stems[i0][i1]));
for(size_t i0=0;i0<4;++i0) out.push_back(static_cast<double>(v.visible_ten_gods[i0]));
for(size_t i0=0;i0<4;++i0) for(size_t i1=0;i1<3;++i1) out.push_back(static_cast<double>(v.hidden_ten_gods[i0][i1]));
for(size_t i0=0;i0<4;++i0) out.push_back(static_cast<double>(v.life_stages[i0]));
for(size_t i0=0;i0<4;++i0) out.push_back(static_cast<double>(v.nayin_ids[i0]));
}
void eclipse_read(taiyin_bazi_chart& v,EclipseReader& in) { taiyin_bazi_chart_init(&v);
v.year_pillar=web_integer<taiyin_ganzhi>(in.next());
v.month_pillar=web_integer<taiyin_ganzhi>(in.next());
v.day_pillar=web_integer<taiyin_ganzhi>(in.next());
v.hour_pillar=web_integer<taiyin_ganzhi>(in.next());
v.ming_gong=web_integer<taiyin_ganzhi>(in.next());
v.shen_gong=web_integer<taiyin_ganzhi>(in.next());
v.tai_yuan=web_integer<taiyin_ganzhi>(in.next());
v.tai_xi=web_integer<taiyin_ganzhi>(in.next());
for(size_t i0=0;i0<4;++i0) v.hidden_stem_count[i0]=web_integer<uint8_t>(in.next());
for(size_t i0=0;i0<4;++i0) for(size_t i1=0;i1<3;++i1) v.hidden_stems[i0][i1]=web_integer<uint8_t>(in.next());
for(size_t i0=0;i0<4;++i0) v.visible_ten_gods[i0]=web_integer<uint8_t>(in.next());
for(size_t i0=0;i0<4;++i0) for(size_t i1=0;i1<3;++i1) v.hidden_ten_gods[i0][i1]=web_integer<uint8_t>(in.next());
for(size_t i0=0;i0<4;++i0) v.life_stages[i0]=web_integer<uint8_t>(in.next());
for(size_t i0=0;i0<4;++i0) v.nayin_ids[i0]=web_integer<uint8_t>(in.next());
}
void eclipse_pack(const taiyin_bazi_relation& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.kind));
out.push_back(static_cast<double>(v.pillar_mask));
out.push_back(static_cast<double>(v.combined_element_id));
}
void eclipse_read(taiyin_bazi_relation& v,EclipseReader& in) { taiyin_bazi_relation_init(&v);
v.kind=web_integer<int32_t>(in.next());
v.pillar_mask=web_integer<uint32_t>(in.next());
v.combined_element_id=web_integer<uint8_t>(in.next());
}
void eclipse_pack(const taiyin_bazi_qiyun_result& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.direction));
out.push_back(static_cast<double>(v.time_model));
out.push_back(static_cast<double>(v.reference_jie_index));
out.push_back(static_cast<double>(v.jie_interval_days));
out.push_back(static_cast<double>(v.start_age_years));
out.push_back(static_cast<double>(v.offset_years));
out.push_back(static_cast<double>(v.offset_months));
out.push_back(static_cast<double>(v.offset_days));
out.push_back(static_cast<double>(v.offset_hours));
out.push_back(static_cast<double>(v.offset_minutes));
out.push_back(static_cast<double>(v.offset_seconds));
eclipse_pack(v.reference_jie_jd_ut,out);
eclipse_pack(v.start_jd_ut,out);
eclipse_pack(v.start_civil_time,out);
}
void eclipse_read(taiyin_bazi_qiyun_result& v,EclipseReader& in) { taiyin_bazi_qiyun_result_init(&v);
v.direction=web_integer<int32_t>(in.next());
v.time_model=web_integer<int32_t>(in.next());
v.reference_jie_index=web_integer<uint8_t>(in.next());
v.jie_interval_days=static_cast<double>(in.next());
v.start_age_years=static_cast<double>(in.next());
v.offset_years=web_integer<int32_t>(in.next());
v.offset_months=web_integer<int32_t>(in.next());
v.offset_days=web_integer<int32_t>(in.next());
v.offset_hours=web_integer<int32_t>(in.next());
v.offset_minutes=web_integer<int32_t>(in.next());
v.offset_seconds=static_cast<double>(in.next());
eclipse_read(v.reference_jie_jd_ut,in);
eclipse_read(v.start_jd_ut,in);
eclipse_read(v.start_civil_time,in);
}
void eclipse_pack(const taiyin_bazi_dayun& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.index));
out.push_back(static_cast<double>(v.ganzhi));
out.push_back(static_cast<double>(v.start_virtual_age));
out.push_back(static_cast<double>(v.end_virtual_age));
eclipse_pack(v.start_jd_ut,out);
eclipse_pack(v.end_jd_ut,out);
eclipse_pack(v.start_civil_time,out);
eclipse_pack(v.end_civil_time,out);
}
void eclipse_read(taiyin_bazi_dayun& v,EclipseReader& in) { taiyin_bazi_dayun_init(&v);
v.index=web_integer<uint32_t>(in.next());
v.ganzhi=web_integer<taiyin_ganzhi>(in.next());
v.start_virtual_age=web_integer<int32_t>(in.next());
v.end_virtual_age=web_integer<int32_t>(in.next());
eclipse_read(v.start_jd_ut,in);
eclipse_read(v.end_jd_ut,in);
eclipse_read(v.start_civil_time,in);
eclipse_read(v.end_civil_time,in);
}
void eclipse_pack(const taiyin_bazi_xiaoyun& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.age));
out.push_back(static_cast<double>(v.ganzhi));
}
void eclipse_read(taiyin_bazi_xiaoyun& v,EclipseReader& in) { taiyin_bazi_xiaoyun_init(&v);
v.age=web_integer<uint32_t>(in.next());
v.ganzhi=web_integer<taiyin_ganzhi>(in.next());
}
void eclipse_pack(const taiyin_bazi_renyuan_siling_segment& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.stem_id));
out.push_back(static_cast<double>(v.origin_kind));
out.push_back(static_cast<double>(v.segment_index));
out.push_back(static_cast<double>(v.start_day));
out.push_back(static_cast<double>(v.end_day));
}
void eclipse_read(taiyin_bazi_renyuan_siling_segment& v,EclipseReader& in) { taiyin_bazi_renyuan_siling_segment_init(&v);
v.stem_id=web_integer<uint8_t>(in.next());
v.origin_kind=web_integer<uint8_t>(in.next());
v.segment_index=web_integer<uint8_t>(in.next());
v.start_day=static_cast<double>(in.next());
v.end_day=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_bazi_renyuan_siling_result& v,std::vector<double>& out) {
out.push_back(static_cast<double>(v.table_model));
out.push_back(static_cast<double>(v.time_model));
out.push_back(static_cast<double>(v.month_branch_id));
out.push_back(static_cast<double>(v.stem_id));
out.push_back(static_cast<double>(v.origin_kind));
out.push_back(static_cast<double>(v.segment_index));
out.push_back(static_cast<double>(v.previous_jie_index));
out.push_back(static_cast<double>(v.days_since_jie));
out.push_back(static_cast<double>(v.segment_start_day));
out.push_back(static_cast<double>(v.segment_end_day));
eclipse_pack(v.previous_jie_jd_ut,out);
}
void eclipse_read(taiyin_bazi_renyuan_siling_result& v,EclipseReader& in) { taiyin_bazi_renyuan_siling_result_init(&v);
v.table_model=web_integer<int32_t>(in.next());
v.time_model=web_integer<int32_t>(in.next());
v.month_branch_id=web_integer<uint8_t>(in.next());
v.stem_id=web_integer<uint8_t>(in.next());
v.origin_kind=web_integer<uint8_t>(in.next());
v.segment_index=web_integer<uint8_t>(in.next());
v.previous_jie_index=web_integer<uint8_t>(in.next());
v.days_since_jie=static_cast<double>(in.next());
v.segment_start_day=static_cast<double>(in.next());
v.segment_end_day=static_cast<double>(in.next());
eclipse_read(v.previous_jie_jd_ut,in);
}

void eclipse_pack(const taiyin_bazi_reverse_candidate& v,std::vector<double>& out) {
eclipse_pack(v.start_jd_ut1,out);
eclipse_pack(v.end_jd_ut1,out);
eclipse_pack(v.start_virtual_time,out);
eclipse_pack(v.end_virtual_time,out);
eclipse_pack(v.pillars,out);
}
template<class T,class F> taiyin_call_result bazi_array(WebEclipseReply& reply,F calculate) {
 size_t count=0; taiyin_call_result result=calculate(static_cast<T*>(0),0,&count);
 const uint32_t flags=taiyin_call_result_flags(result);
 if(taiyin_call_result_status(result)!=0)return result;
 if(count>1000000)return taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT,flags);
 std::vector<T> output(count); const size_t capacity=count;
 for(size_t i=0;i<count;++i) output[i].struct_size=0;
 if(count) result=calculate(output.data(),capacity,&count);
 if(taiyin_call_result_status(result)==0) {
  if(count>capacity) return taiyin_make_call_result(TAIYIN_ERROR_INTERNAL,flags);
  reply.values[2]=static_cast<double>(count);
  for(size_t i=0;i<count;++i)eclipse_pack(output[i],reply.values);
 }
 return taiyin_make_call_result(taiyin_call_result_status(result),flags|taiyin_call_result_flags(result));
}
template<class T> T bazi_read(EclipseReader& in) { T value; eclipse_read(value,in); return value; }
void bazi_string(std::vector<double>& out,const char* text) {
 const size_t size=std::char_traits<char>::length(text);out.push_back(static_cast<double>(size));
 for(size_t i=0;i<size;++i)out.push_back(static_cast<unsigned char>(text[i]));
}
}
extern "C" {
WebEclipseReply* taiyin_web_bazi(taiyin_bazi_context* context,const taiyin_chinese_calendar_context* calendar,int operation,const double* input,size_t input_count) {
 try {
  std::unique_ptr<WebEclipseReply> reply(new WebEclipseReply());reply->values.reserve(64);
  EclipseReader in={input,input_count,0};
  taiyin_ephemeris_diagnostic diag; taiyin_ephemeris_diagnostic_init(&diag);
  taiyin_call_result result=taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT,0);
  switch(operation) {
   case 0: { auto config=bazi_read<taiyin_bazi_context_config>(in); taiyin_bazi_context* created=0;
    result=taiyin_bazi_context_create(&config,&created);reply->values.push_back(static_cast<double>(reinterpret_cast<uintptr_t>(created)));break; }
   case 1: { const auto value=web_integer<taiyin_ganzhi>(in.next());uint8_t branches[2]={0,0};
    result=taiyin_bazi_get_kong_wang(value,branches);reply->values.push_back(branches[0]);reply->values.push_back(branches[1]);break; }
   case 2: { const uint8_t a=web_integer<uint8_t>(in.next()),b=web_integer<uint8_t>(in.next()); uint8_t value=0;
    result=taiyin_bazi_get_ten_god(a,b,&value);reply->values.push_back(value);break; }
   case 3: { const uint8_t branch=web_integer<uint8_t>(in.next());uint8_t stems[3]={0,0,0},count=0;
    result=taiyin_bazi_get_hidden_stems(branch,stems,&count);reply->values.push_back(count);for(int i=0;i<3;++i)reply->values.push_back(stems[i]);break; }
   case 4: case 5: case 6: { const uint8_t a=web_integer<uint8_t>(in.next()),b=web_integer<uint8_t>(in.next());uint32_t flags=0;uint8_t element=0;
    if(operation==4)result=taiyin_bazi_calc_stem_relation(a,b,&flags,&element);
    else if(operation==5)result=taiyin_bazi_calc_branch_relation(a,b,&flags,&element);
    else { const uint8_t c=web_integer<uint8_t>(in.next());result=taiyin_bazi_calc_branch_triple_relation(a,b,c,&flags,&element); }
    reply->values.push_back(flags);reply->values.push_back(element);break; }
   case 7: { const uint8_t stem=web_integer<uint8_t>(in.next()),branch=web_integer<uint8_t>(in.next());const int mode=web_integer<int>(in.next());uint8_t value=0;
    result=taiyin_bazi_get_life_stage(stem,branch,mode,&value);reply->values.push_back(value);break; }
   case 8: { const auto pillars=bazi_read<taiyin_ganzhi_four_pillars>(in);taiyin_bazi_chart chart;taiyin_bazi_chart_init(&chart);
    result=taiyin_bazi_calc_chart(context,&pillars,&chart);eclipse_pack(chart,reply->values);break; }
   case 9: case 10: case 11: case 12: case 13: { taiyin_ganzhi value=0;
    if(operation==9) result=taiyin_bazi_calc_liunian(web_integer<int32_t>(in.next()),&value);
    else if(operation==10 || operation==12) { const auto pillar=web_integer<taiyin_ganzhi>(in.next()); const auto branch=web_integer<uint8_t>(in.next());
     result=operation==10?taiyin_bazi_calc_liuyue(pillar,branch,&value):taiyin_bazi_calc_liushi(pillar,branch,&value); }
    else if(operation==11) {const auto date=bazi_read<taiyin_calendar_datetime>(in);result=taiyin_bazi_calc_liuri(&date,&value);}
    else {const auto chart=bazi_read<taiyin_bazi_chart>(in);const int direction=web_integer<int>(in.next()),age=web_integer<int>(in.next());result=taiyin_bazi_calc_xiaoyun(&chart,direction,age,&value);}
    reply->values.push_back(value);break; }
   case 14: { const auto chart=bazi_read<taiyin_bazi_chart>(in);const int direction=web_integer<int>(in.next()),start=web_integer<int>(in.next());const size_t requested=web_integer<size_t>(in.next());
    result=bazi_array<taiyin_bazi_xiaoyun>(*reply,[&](taiyin_bazi_xiaoyun* out,size_t capacity,size_t* count){return taiyin_bazi_fill_xiaoyun(&chart,direction,start,requested,out,capacity,count);});break; }
   case 15: { const auto jd=bazi_read<taiyin_split_julian_date>(in);const auto date=bazi_read<taiyin_calendar_datetime>(in);const auto chart=bazi_read<taiyin_bazi_chart>(in);const int gender=web_integer<int>(in.next());
    taiyin_bazi_qiyun_result qiyun;taiyin_bazi_qiyun_result_init(&qiyun);result=taiyin_bazi_calc_qiyun(context,calendar,&jd,&date,&chart,gender,&qiyun,&diag);eclipse_pack(qiyun,reply->values);break; }
   case 16: { const auto date=bazi_read<taiyin_calendar_datetime>(in);const auto chart=bazi_read<taiyin_bazi_chart>(in);const auto qiyun=bazi_read<taiyin_bazi_qiyun_result>(in);const size_t requested=web_integer<size_t>(in.next());
    result=bazi_array<taiyin_bazi_dayun>(*reply,[&](taiyin_bazi_dayun* out,size_t capacity,size_t* count){return taiyin_bazi_fill_dayun(context,&date,&chart,&qiyun,requested,out,capacity,count);});break; }
   case 17: { const auto jd=bazi_read<taiyin_split_julian_date>(in);const auto chart=bazi_read<taiyin_bazi_chart>(in);const int table=web_integer<int>(in.next()),time=web_integer<int>(in.next());
    taiyin_bazi_renyuan_siling_result value;taiyin_bazi_renyuan_siling_result_init(&value);result=taiyin_bazi_calc_renyuan_siling(calendar,&jd,&chart,table,time,&value,&diag);eclipse_pack(value,reply->values);break; }
   case 18: { const uint8_t branch=web_integer<uint8_t>(in.next());const int table=web_integer<int>(in.next());
    result=bazi_array<taiyin_bazi_renyuan_siling_segment>(*reply,[&](taiyin_bazi_renyuan_siling_segment* out,size_t capacity,size_t* count){return taiyin_bazi_get_renyuan_siling_segments(branch,table,out,capacity,count);});break; }
   case 19: {const auto chart=bazi_read<taiyin_bazi_chart>(in);const uint32_t pillars=web_integer<uint32_t>(in.next()),relations=web_integer<uint32_t>(in.next());
    result=bazi_array<taiyin_bazi_relation>(*reply,[&](taiyin_bazi_relation* out,size_t capacity,size_t* count){return taiyin_bazi_collect_chart_relations(&chart,pillars,relations,out,capacity,count);});break; }
   case 20: {const auto chart=bazi_read<taiyin_bazi_chart>(in);const auto target=web_integer<taiyin_ganzhi>(in.next());const int kind=web_integer<int>(in.next()),gender=web_integer<int>(in.next());uint64_t words[2]={0,0};size_t count=0;
    result=gender<0?taiyin_bazi_collect_target_shen_sha(&chart,target,kind,words,2,&count):taiyin_bazi_collect_target_shen_sha_with_gender(&chart,target,kind,gender,words,2,&count);
    if(taiyin_call_result_status(result)==0) {if(count<1 || count>2)throw std::runtime_error("invalid Shen Sha word count");for(size_t w=0;w<count;++w)for(int b=0;b<64;++b)if(words[w]&(UINT64_C(1)<<b)){reply->values.push_back(static_cast<double>(w*64+b));reply->values[2]++;}}break; }
   case 21: {const auto chart=bazi_read<taiyin_bazi_chart>(in);const auto target=web_integer<taiyin_ganzhi>(in.next());const int kind=web_integer<int>(in.next()),gender=web_integer<int>(in.next());
    taiyin_bazi_shen_sha_catalog* catalog=0;taiyin_bazi_shen_sha_context* shen=0;taiyin_bazi_shen_sha_matches* matches=0;
    std::unique_ptr<taiyin_bazi_shen_sha_catalog,decltype(&taiyin_bazi_shen_sha_catalog_destroy)> catalog_owner(0,&taiyin_bazi_shen_sha_catalog_destroy);
    std::unique_ptr<taiyin_bazi_shen_sha_context,decltype(&taiyin_bazi_shen_sha_context_destroy)> shen_owner(0,&taiyin_bazi_shen_sha_context_destroy);
    std::unique_ptr<taiyin_bazi_shen_sha_matches,decltype(&taiyin_bazi_shen_sha_matches_destroy)> matches_owner(0,&taiyin_bazi_shen_sha_matches_destroy);
    result=taiyin_bazi_shen_sha_catalog_create(&catalog);catalog_owner.reset(catalog);
    if(taiyin_call_result_status(result)==0)result=taiyin_bazi_shen_sha_context_create(catalog,0,0,&shen);shen_owner.reset(shen);
    if(taiyin_call_result_status(result)==0)result=taiyin_bazi_shen_sha_evaluate(shen,&chart,target,kind,gender,&matches);matches_owner.reset(matches);
    if(taiyin_call_result_status(result)==0){const size_t count=taiyin_bazi_shen_sha_matches_count(matches);reply->values[2]=static_cast<double>(count);for(size_t i=0;i<count;++i){const char* id=0;const char* name=0;int32_t builtin=-1;result=taiyin_bazi_shen_sha_matches_get(matches,i,&id,&name,&builtin);if(taiyin_call_result_status(result)!=0)break;reply->values.push_back(builtin);bazi_string(reply->values,id);bazi_string(reply->values,name);}}
    break; }
   case 22: {taiyin_bazi_shen_sha_catalog* value=0;result=taiyin_bazi_shen_sha_catalog_create(&value);reply->values.push_back(static_cast<double>(reinterpret_cast<uintptr_t>(value)));break;}
   case 23: {const std::string label=bazi_text(in);const size_t count=web_integer<size_t>(in.next());
    if(count>1000000)throw std::invalid_argument("too many Shen Sha rules");
    std::vector<std::string> ids,names;std::vector<uintptr_t> tokens;ids.reserve(count);names.reserve(count);tokens.reserve(count);
    for(size_t i=0;i<count;++i){ids.push_back(bazi_text(in));names.push_back(bazi_text(in));tokens.push_back(web_integer<uintptr_t>(in.next()));}
    std::vector<taiyin_bazi_shen_sha_rule> rules(count);
    for(size_t i=0;i<count;++i){rules[i].struct_size=sizeof(rules[i]);rules[i].id=ids[i].c_str();rules[i].name=names[i].c_str();rules[i].predicate=&web_bazi_rule;rules[i].user_data=reinterpret_cast<void*>(tokens[i]);}
    taiyin_bazi_shen_sha_catalog* value=0;result=taiyin_bazi_shen_sha_catalog_add_module(reinterpret_cast<const taiyin_bazi_shen_sha_catalog*>(context),label.c_str(),rules.data(),count,&value);
    reply->values.push_back(static_cast<double>(reinterpret_cast<uintptr_t>(value)));break;}
   case 24: {const std::string label=bazi_text(in);taiyin_bazi_shen_sha_catalog* value=0;
    result=taiyin_bazi_shen_sha_catalog_remove_module(reinterpret_cast<const taiyin_bazi_shen_sha_catalog*>(context),label.c_str(),&value);reply->values.push_back(static_cast<double>(reinterpret_cast<uintptr_t>(value)));break;}
   case 25: {const size_t count=web_integer<size_t>(in.next());if(count>1000000)throw std::invalid_argument("too many disabled IDs");
    std::vector<std::string> ids;ids.reserve(count);for(size_t i=0;i<count;++i)ids.push_back(bazi_text(in));std::vector<const char*> pointers;for(size_t i=0;i<count;++i)pointers.push_back(ids[i].c_str());
    taiyin_bazi_shen_sha_context* value=0;result=taiyin_bazi_shen_sha_context_create(reinterpret_cast<const taiyin_bazi_shen_sha_catalog*>(context),pointers.data(),count,&value);reply->values.push_back(static_cast<double>(reinterpret_cast<uintptr_t>(value)));break;}
   case 26: {const auto chart=bazi_read<taiyin_bazi_chart>(in);const auto target=web_integer<taiyin_ganzhi>(in.next());const int kind=web_integer<int>(in.next()),gender=web_integer<int>(in.next());
    taiyin_bazi_shen_sha_matches* matches=0;result=taiyin_bazi_shen_sha_evaluate(reinterpret_cast<const taiyin_bazi_shen_sha_context*>(context),&chart,target,kind,gender,&matches);
    std::unique_ptr<taiyin_bazi_shen_sha_matches,decltype(&taiyin_bazi_shen_sha_matches_destroy)> owned_matches(matches,&taiyin_bazi_shen_sha_matches_destroy);
    if(taiyin_call_result_status(result)==0){const size_t count=taiyin_bazi_shen_sha_matches_count(matches);reply->values[2]=static_cast<double>(count);for(size_t i=0;i<count;++i){const char* id=0;const char* name=0;int32_t builtin=-1;result=taiyin_bazi_shen_sha_matches_get(matches,i,&id,&name,&builtin);if(taiyin_call_result_status(result)!=0)break;reply->values.push_back(builtin);bazi_string(reply->values,id);bazi_string(reply->values,name);}}break;}
   case 27: taiyin_bazi_shen_sha_catalog_destroy(reinterpret_cast<taiyin_bazi_shen_sha_catalog*>(context));result=taiyin_make_call_result(TAIYIN_STATUS_OK,0);break;
   case 28: taiyin_bazi_shen_sha_context_destroy(reinterpret_cast<taiyin_bazi_shen_sha_context*>(context));result=taiyin_make_call_result(TAIYIN_STATUS_OK,0);break;
   case 29: {
    // This new entry checks all narrowing inputs before legacy serializers.
    if(in.count!=11)throw std::invalid_argument("invalid reverse input length");
    for(size_t i=0;i<in.count;++i) {
     const double v=in.input[i];if(!std::isfinite(v))throw std::invalid_argument("nonfinite reverse input");
     if(i==0||i==2){if(std::floor(v)!=v||std::fabs(v)>9007199254740991.0)throw std::invalid_argument("invalid reverse JD");}
     else if(i>=4&&i<=9){if(std::floor(v)!=v||v<0||v>(i<8?255:2))throw std::invalid_argument("invalid reverse integer");}
    }
    taiyin_bazi_reverse_request request;taiyin_bazi_reverse_request_init(&request);
    eclipse_read(request.start_jd_ut1,in);eclipse_read(request.end_jd_ut1,in);
    eclipse_read(request.pillars,in);
    request.rat_hour_mode=web_integer<int32_t>(in.next());
    request.clock_mode=web_integer<int32_t>(in.next());request.longitude_rad=in.next();
    result=bazi_array<taiyin_bazi_reverse_candidate>(*reply,[&](taiyin_bazi_reverse_candidate* out,size_t capacity,size_t* count){return taiyin_bazi_reverse_lookup_at_ut1(calendar,&request,out,capacity,count,&diag);});break;
   }
  }
  if(in.cursor!=in.count) throw std::invalid_argument("extra BaZi input");
  diagnostic(diag,reply->values.data());reply->values[0]=taiyin_call_result_flags(result);reply->values[1]=taiyin_call_result_status(result);return reply.release();
 }catch(const std::bad_alloc&){return 0;}catch(const std::invalid_argument&){return web_error_reply(TAIYIN_ERROR_INVALID_ARGUMENT);}catch(...){return web_error_reply(TAIYIN_ERROR_INTERNAL);}
}
}
