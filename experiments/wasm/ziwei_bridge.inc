#include "taiyin/c/ziwei.h"
namespace {
void eclipse_pack(const taiyin_ziwei_chart_clock& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.mode));
out.push_back(static_cast<double>(v.longitude_rad));
}
void eclipse_read(taiyin_ziwei_chart_clock& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
v.mode=web_integer<int32_t>(in.next());
v.longitude_rad=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_ziwei_placement_input& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.year_stem));
out.push_back(static_cast<double>(v.year_branch));
out.push_back(static_cast<double>(v.month));
out.push_back(static_cast<double>(v.day));
out.push_back(static_cast<double>(v.hour_branch));
}
void eclipse_read(taiyin_ziwei_placement_input& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
v.year_stem=web_integer<int32_t>(in.next());
v.year_branch=web_integer<int32_t>(in.next());
v.month=web_integer<int32_t>(in.next());
v.day=web_integer<int32_t>(in.next());
v.hour_branch=web_integer<int32_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_placement_patch& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.year_stem));
out.push_back(static_cast<double>(v.year_branch));
out.push_back(static_cast<double>(v.month));
out.push_back(static_cast<double>(v.day));
out.push_back(static_cast<double>(v.hour_branch));
out.push_back(static_cast<double>(v.update_bureau));
}
void eclipse_read(taiyin_ziwei_placement_patch& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
v.year_stem=web_integer<int32_t>(in.next());
v.year_branch=web_integer<int32_t>(in.next());
v.month=web_integer<int32_t>(in.next());
v.day=web_integer<int32_t>(in.next());
v.hour_branch=web_integer<int32_t>(in.next());
v.update_bureau=web_integer<int32_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_casting_options& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.gender));
out.push_back(static_cast<double>(v.chart_mode));
out.push_back(static_cast<double>(v.fixed_bureau));
}
void eclipse_read(taiyin_ziwei_casting_options& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
v.gender=web_integer<int32_t>(in.next());
v.chart_mode=web_integer<int32_t>(in.next());
v.fixed_bureau=web_integer<int32_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_casting_summary& v,std::vector<double>& out){
eclipse_pack(v.input,out);
eclipse_pack(v.original_input,out);
eclipse_pack(v.overrides,out);
out.push_back(static_cast<double>(v.index));
out.push_back(static_cast<double>(v.method));
out.push_back(static_cast<double>(v.gender));
out.push_back(static_cast<double>(v.chart_mode));
out.push_back(static_cast<double>(v.bureau));
out.push_back(static_cast<double>(v.original_bureau));
out.push_back(static_cast<double>(v.body_palace));
out.push_back(static_cast<double>(v.update_bureau));
out.push_back(static_cast<double>(v.life_palace_shift));
out.push_back(static_cast<double>(v.year_transform_stem));
for(size_t i=0;i<12;++i) out.push_back(static_cast<double>(v.palace_branches[i]));
for(size_t i=0;i<12;++i) out.push_back(static_cast<double>(v.palace_stems[i]));
out.push_back(static_cast<double>(v.life_master));
out.push_back(static_cast<double>(v.body_master));
out.push_back(static_cast<double>(v.year_lu));
out.push_back(static_cast<double>(v.year_quan));
out.push_back(static_cast<double>(v.year_ke));
out.push_back(static_cast<double>(v.year_ji));
}
void eclipse_read(taiyin_ziwei_casting_summary& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
eclipse_read(v.input,in);
eclipse_read(v.original_input,in);
eclipse_read(v.overrides,in);
v.index=web_integer<uint32_t>(in.next());
v.method=web_integer<uint8_t>(in.next());
v.gender=web_integer<uint8_t>(in.next());
v.chart_mode=web_integer<uint8_t>(in.next());
v.bureau=web_integer<uint8_t>(in.next());
v.original_bureau=web_integer<uint8_t>(in.next());
v.body_palace=web_integer<uint8_t>(in.next());
v.update_bureau=web_integer<uint8_t>(in.next());
v.life_palace_shift=web_integer<uint8_t>(in.next());
v.year_transform_stem=web_integer<uint8_t>(in.next());
for(size_t i=0;i<12;++i) v.palace_branches[i]=web_integer<uint8_t>(in.next());
for(size_t i=0;i<12;++i) v.palace_stems[i]=web_integer<uint8_t>(in.next());
v.life_master=web_integer<uint16_t>(in.next());
v.body_master=web_integer<uint16_t>(in.next());
v.year_lu=web_integer<uint16_t>(in.next());
v.year_quan=web_integer<uint16_t>(in.next());
v.year_ke=web_integer<uint16_t>(in.next());
v.year_ji=web_integer<uint16_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_omitted_placement& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.star_id));
out.push_back(static_cast<double>(v.missing_inputs));
}
void eclipse_read(taiyin_ziwei_omitted_placement& v,EclipseReader& in){ v={};
v.star_id=web_integer<uint16_t>(in.next());
v.missing_inputs=web_integer<uint32_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_birth_options& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.rat_hour_mode));
out.push_back(static_cast<double>(v.leap_month_strategy));
out.push_back(static_cast<double>(v.chart_mode));
out.push_back(static_cast<double>(v.wu_hu_dun_year_boundary));
out.push_back(static_cast<double>(v.sihua_year_boundary));
out.push_back(static_cast<double>(v.body_master_year_boundary));
}
void eclipse_read(taiyin_ziwei_birth_options& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
v.rat_hour_mode=web_integer<int32_t>(in.next());
v.leap_month_strategy=web_integer<int32_t>(in.next());
v.chart_mode=web_integer<int32_t>(in.next());
v.wu_hu_dun_year_boundary=web_integer<int32_t>(in.next());
v.sihua_year_boundary=web_integer<int32_t>(in.next());
v.body_master_year_boundary=web_integer<int32_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_flow_options& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.boundary));
out.push_back(static_cast<double>(v.rat_hour_mode));
out.push_back(static_cast<double>(v.childhood_strategy));
out.push_back(static_cast<double>(v.flow_month_palace_strategy));
}
void eclipse_read(taiyin_ziwei_flow_options& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
v.boundary=web_integer<int32_t>(in.next());
v.rat_hour_mode=web_integer<int32_t>(in.next());
v.childhood_strategy=web_integer<int32_t>(in.next());
v.flow_month_palace_strategy=web_integer<int32_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_transform_set& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.lu));
out.push_back(static_cast<double>(v.quan));
out.push_back(static_cast<double>(v.ke));
out.push_back(static_cast<double>(v.ji));
}
void eclipse_read(taiyin_ziwei_transform_set& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
v.lu=web_integer<uint16_t>(in.next());
v.quan=web_integer<uint16_t>(in.next());
v.ke=web_integer<uint16_t>(in.next());
v.ji=web_integer<uint16_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_flow_summary& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.effective_birth_year));
out.push_back(static_cast<double>(v.effective_target_year));
out.push_back(static_cast<double>(v.target_month));
out.push_back(static_cast<double>(v.target_month_sequence));
out.push_back(static_cast<double>(v.target_day));
out.push_back(static_cast<double>(v.target_hour_index));
out.push_back(static_cast<double>(v.target_rat_hour_segment));
out.push_back(static_cast<double>(v.target_month_is_leap));
out.push_back(static_cast<double>(v.decade_index));
out.push_back(static_cast<double>(v.decade_start_age));
out.push_back(static_cast<double>(v.decade_end_age));
out.push_back(static_cast<double>(v.small_limit_virtual_age));
out.push_back(static_cast<double>(v.small_limit_stem));
out.push_back(static_cast<double>(v.small_limit_branch));
out.push_back(static_cast<double>(v.target_month_building_branch));
out.push_back(static_cast<double>(v.target_effective_month));
out.push_back(static_cast<double>(v.target_month_name));
out.push_back(static_cast<double>(v.target_palace_month_index));
out.push_back(static_cast<double>(v.target_lunar_year));
}
void eclipse_read(taiyin_ziwei_flow_summary& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
v.effective_birth_year=web_integer<int32_t>(in.next());
v.effective_target_year=web_integer<int32_t>(in.next());
v.target_month=web_integer<uint8_t>(in.next());
v.target_month_sequence=web_integer<uint8_t>(in.next());
v.target_day=web_integer<uint8_t>(in.next());
v.target_hour_index=web_integer<uint8_t>(in.next());
v.target_rat_hour_segment=web_integer<uint8_t>(in.next());
v.target_month_is_leap=web_integer<uint8_t>(in.next());
v.decade_index=web_integer<uint16_t>(in.next());
v.decade_start_age=web_integer<int32_t>(in.next());
v.decade_end_age=web_integer<int32_t>(in.next());
v.small_limit_virtual_age=web_integer<int32_t>(in.next());
v.small_limit_stem=web_integer<uint8_t>(in.next());
v.small_limit_branch=web_integer<uint8_t>(in.next());
v.target_month_building_branch=web_integer<uint8_t>(in.next());
v.target_effective_month=web_integer<uint8_t>(in.next());
v.target_month_name=web_integer<uint8_t>(in.next());
v.target_palace_month_index=web_integer<uint8_t>(in.next());
v.target_lunar_year=web_integer<int32_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_reverse_query& v,std::vector<double>& out){
out.push_back(static_cast<double>(v.lucun_branch));
out.push_back(static_cast<double>(v.hongluan_branch));
out.push_back(static_cast<double>(v.zuofu_branch));
out.push_back(static_cast<double>(v.youbi_branch));
out.push_back(static_cast<double>(v.wenchang_branch));
out.push_back(static_cast<double>(v.wenqu_branch));
out.push_back(static_cast<double>(v.santai_branch));
out.push_back(static_cast<double>(v.bazuo_branch));
out.push_back(static_cast<double>(v.ziwei_branch));
}
void eclipse_read(taiyin_ziwei_reverse_query& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
v.lucun_branch=web_integer<int32_t>(in.next());
v.hongluan_branch=web_integer<int32_t>(in.next());
v.zuofu_branch=web_integer<int32_t>(in.next());
v.youbi_branch=web_integer<int32_t>(in.next());
v.wenchang_branch=web_integer<int32_t>(in.next());
v.wenqu_branch=web_integer<int32_t>(in.next());
v.santai_branch=web_integer<int32_t>(in.next());
v.bazuo_branch=web_integer<int32_t>(in.next());
v.ziwei_branch=web_integer<int32_t>(in.next());
}
void eclipse_pack(const taiyin_ziwei_reverse_request& v,std::vector<double>& out){
eclipse_pack(v.start_instant_utc,out);
eclipse_pack(v.end_instant_utc,out);
eclipse_pack(v.start_virtual_time,out);
out.push_back(static_cast<double>(v.gender));
eclipse_pack(v.birth_options,out);
eclipse_pack(v.query,out);
}
void eclipse_read(taiyin_ziwei_reverse_request& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
eclipse_read(v.start_instant_utc,in);
eclipse_read(v.end_instant_utc,in);
eclipse_read(v.start_virtual_time,in);
v.gender=web_integer<int32_t>(in.next());
eclipse_read(v.birth_options,in);
eclipse_read(v.query,in);
}
void eclipse_pack(const taiyin_ziwei_reverse_candidate& v,std::vector<double>& out){
eclipse_pack(v.instant_utc,out);
eclipse_pack(v.virtual_time,out);
out.push_back(static_cast<double>(v.lunar_year));
out.push_back(static_cast<double>(v.lunar_month));
out.push_back(static_cast<double>(v.lunar_day));
out.push_back(static_cast<double>(v.lunar_is_leap));
out.push_back(static_cast<double>(v.hour_branch));
out.push_back(static_cast<double>(v.rat_hour_segment));
}
void eclipse_read(taiyin_ziwei_reverse_candidate& v,EclipseReader& in){ v={};v.struct_size=sizeof(v);
eclipse_read(v.instant_utc,in);
eclipse_read(v.virtual_time,in);
v.lunar_year=web_integer<int32_t>(in.next());
v.lunar_month=web_integer<uint8_t>(in.next());
v.lunar_day=web_integer<uint8_t>(in.next());
v.lunar_is_leap=web_integer<uint8_t>(in.next());
v.hour_branch=web_integer<uint8_t>(in.next());
v.rat_hour_segment=web_integer<uint8_t>(in.next());
}

void ziwei_generation(uint64_t value,std::vector<double>& out){out.push_back(static_cast<uint32_t>(value));out.push_back(static_cast<uint32_t>(value>>32));}
template<class T,class F> taiyin_call_result ziwei_array(WebEclipseReply& reply,F calculate){
 size_t count=0;auto result=calculate(static_cast<T*>(0),0,&count);uint32_t flags=taiyin_call_result_flags(result);
 if(taiyin_call_result_status(result))return result;
 if(count>1000000)throw std::invalid_argument("too many Ziwei results");
 std::vector<T> values(count);for(size_t i=0;i<count;++i)values[i].struct_size=sizeof(T);
 const size_t capacity=count;if(count)result=calculate(values.data(),capacity,&count);
 if(taiyin_call_result_status(result)==0){if(count>capacity)throw std::runtime_error("invalid Ziwei count");reply.values[2]=count;for(size_t i=0;i<count;++i)eclipse_pack(values[i],reply.values);}
 return taiyin_make_call_result(taiyin_call_result_status(result),flags|taiyin_call_result_flags(result));
}
template<class F> taiyin_call_result ziwei_ids(WebEclipseReply& reply,F calculate){size_t count=0;auto result=calculate(static_cast<uint16_t*>(0),0,&count);const uint32_t flags=taiyin_call_result_flags(result);if(taiyin_call_result_status(result))return result;if(count>1000000)throw std::invalid_argument("too many stars");std::vector<uint16_t> ids(count);const size_t capacity=count;if(count)result=calculate(ids.data(),capacity,&count);if(taiyin_call_result_status(result)==0){if(count>capacity)throw std::runtime_error("invalid star count");reply.values[2]=count;for(size_t i=0;i<count;++i)reply.values.push_back(ids[i]);}return taiyin_make_call_result(taiyin_call_result_status(result),flags|taiyin_call_result_flags(result));}
template<class F> taiyin_call_result ziwei_omitted(WebEclipseReply& reply,F calculate){size_t count=0;auto result=calculate(static_cast<taiyin_ziwei_omitted_placement*>(0),0,&count);const uint32_t flags=taiyin_call_result_flags(result);if(taiyin_call_result_status(result))return result;if(count>1000000)throw std::invalid_argument("too many omitted stars");std::vector<taiyin_ziwei_omitted_placement> values(count);const size_t capacity=count;if(count)result=calculate(values.data(),capacity,&count);if(taiyin_call_result_status(result)==0){if(count>capacity)throw std::runtime_error("invalid omitted count");reply.values[2]=count;for(size_t i=0;i<count;++i)eclipse_pack(values[i],reply.values);}return taiyin_make_call_result(taiyin_call_result_status(result),flags|taiyin_call_result_flags(result));}
}
extern "C" {
WebEclipseReply* taiyin_web_ziwei(uintptr_t handle,uintptr_t chart_handle,const taiyin_chinese_calendar_context* calendar,int operation,const double* input,size_t input_count){
 try{
 std::unique_ptr<WebEclipseReply> reply(new WebEclipseReply());reply->values.reserve(512);
 EclipseReader in={input,input_count,0};taiyin_ephemeris_diagnostic diag;taiyin_ephemeris_diagnostic_init(&diag);
 auto result=taiyin_make_call_result(TAIYIN_STATUS_OK,0);
 auto context=reinterpret_cast<taiyin_ziwei_context*>(handle);auto chart=reinterpret_cast<taiyin_ziwei_chart*>(chart_handle);auto casting=reinterpret_cast<taiyin_ziwei_casting_chart*>(chart_handle);auto catalog=reinterpret_cast<taiyin_ziwei_data_catalog*>(handle);
 auto& out=reply->values;
 switch(operation){
 case 0:{const auto path=bazi_text(in);taiyin_ziwei_data_catalog* value=0;result=taiyin_ziwei_data_catalog_create(path.c_str(),&value);out.push_back(reinterpret_cast<uintptr_t>(value));break;}
 case 1:taiyin_ziwei_data_catalog_destroy(catalog);break;
 case 2:result=taiyin_ziwei_data_catalog_reload(catalog);break;
 case 3:ziwei_generation(taiyin_ziwei_data_catalog_generation(catalog),out);break;
 case 4:{const size_t count=web_integer<size_t>(in.next());if(count>1000000)throw std::invalid_argument("too many overrides");std::vector<int> components;std::vector<std::string> keys,options;components.reserve(count);keys.reserve(count);options.reserve(count);for(size_t i=0;i<count;++i){components.push_back(web_integer<int>(in.next()));keys.push_back(bazi_text(in));options.push_back(bazi_text(in));}std::vector<taiyin_ziwei_option_override> overrides(count);for(size_t i=0;i<count;++i){taiyin_ziwei_option_override_init(&overrides[i]);overrides[i].component=components[i];overrides[i].key=keys[i].c_str();overrides[i].option=options[i].c_str();}
 taiyin_ziwei_ruleset* raw=0;result=taiyin_ziwei_ruleset_create(&raw);std::unique_ptr<taiyin_ziwei_ruleset,decltype(&taiyin_ziwei_ruleset_destroy)> rules(raw,&taiyin_ziwei_ruleset_destroy);
 const size_t modules=web_integer<size_t>(in.next());if(modules>1000000)throw std::invalid_argument("too many modules");for(size_t i=0;i<modules;++i){std::string text[6];for(int j=0;j<6;++j)text[j]=bazi_text(in);taiyin_ziwei_json_rule_module module;taiyin_ziwei_json_rule_module_init(&module);module.label=text[0].c_str();module.stars_json=text[1].c_str();module.brightness_json=text[2].c_str();module.sihua_json=text[3].c_str();module.flow_json=text[4].c_str();module.masters_json=text[5].c_str();if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_ruleset_add_json_module(rules.get(),&module);}
 taiyin_ziwei_context* value=0;if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_context_create_with_ruleset(catalog,overrides.data(),count,rules.get(),&value);out.push_back(reinterpret_cast<uintptr_t>(value));break;}
 case 5:taiyin_ziwei_context_destroy(context);break;
 case 6:ziwei_generation(taiyin_ziwei_context_generation(context),out);out.push_back(taiyin_ziwei_star_count(context));break;
 case 7:case 8:{uint16_t id=TAIYIN_ZIWEI_INVALID_STAR_ID;if(operation==7){const auto key=bazi_text(in);result=taiyin_ziwei_find_star(context,key.c_str(),&id);if(taiyin_call_result_status(result)==TAIYIN_ERROR_INVALID_ARGUMENT&&id==TAIYIN_ZIWEI_INVALID_STAR_ID){result=taiyin_make_call_result(TAIYIN_STATUS_OK,0);out.push_back(id);break;}}else id=web_integer<uint16_t>(in.next());out.push_back(id);int32_t category=0;size_t size=0;if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_get_star_metadata(context,id,&category,0,0,&size);if(taiyin_call_result_status(result)==0){std::vector<char> text(size);result=taiyin_ziwei_get_star_metadata(context,id,&category,text.data(),text.size(),&size);uint8_t natal=0;if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_star_is_natal(context,id,&natal);out.push_back(category);out.push_back(natal);bazi_string(out,text.data());}break;}
 case 9:case 10:{const auto jd=bazi_read<taiyin_split_julian_date>(in);taiyin_calendar_datetime time={};taiyin_ziwei_chart_clock clock={};if(operation==9)eclipse_read(time,in);else eclipse_read(clock,in);const int gender=web_integer<int>(in.next());const auto options=bazi_read<taiyin_ziwei_birth_options>(in);taiyin_ziwei_chart* value=0;result=operation==9?taiyin_ziwei_chart_create(context,calendar,&jd,&time,gender,&options,&value,&diag):taiyin_ziwei_chart_create_at_ut1(context,calendar,&jd,&clock,gender,&options,&value,&diag);out.push_back(reinterpret_cast<uintptr_t>(value));break;}
 case 11:taiyin_ziwei_chart_destroy(chart);break;
 case 12:{uint8_t anchors[31]={};result=taiyin_ziwei_chart_get_anchors(chart,anchors);for(int i=0;i<31;++i)out.push_back(anchors[i]);uint8_t gender=0,bureau=0,body=0;uint16_t life_master=0,body_master=0;taiyin_ziwei_transform_set transforms;taiyin_ziwei_transform_set_init(&transforms);if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_chart_get_summary(chart,&gender,&bureau,&body,&life_master,&body_master,&transforms);out.push_back(gender);out.push_back(bureau);out.push_back(body);out.push_back(life_master);out.push_back(body_master);eclipse_pack(transforms,out);for(uint8_t i=0;i<12;++i){uint8_t branch=0,stem=0;if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_chart_get_palace_branch(chart,i,&branch);if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_chart_get_palace_stem(chart,branch,&stem);out.push_back(branch);out.push_back(stem);}taiyin_ziwei_placement_input value;taiyin_ziwei_placement_input_init(&value);taiyin_ziwei_placement_patch patch;taiyin_ziwei_placement_patch_init(&patch);uint8_t shift=0;if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_chart_get_placement(chart,&value,&patch,&shift);eclipse_pack(value,out);eclipse_pack(patch,out);out.push_back(shift);break;}
 case 13:{const uint16_t id=web_integer<uint16_t>(in.next());uint8_t branch=255,palace=255;int32_t brightness=-1;uint16_t mask=0;result=taiyin_ziwei_chart_get_star_position(chart,id,&branch);if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_chart_get_star_palace(chart,id,&palace);if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_chart_get_brightness(context,chart,id,&brightness);if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_chart_get_star_transformation_mask(chart,id,&mask);out.push_back(branch);out.push_back(palace);out.push_back(brightness);out.push_back(mask);break;}
 case 14:{const uint8_t branch=web_integer<uint8_t>(in.next());result=ziwei_ids(*reply,[&](uint16_t* ids,size_t capacity,size_t* count){return taiyin_ziwei_chart_get_palace_stars(chart,branch,ids,capacity,count);});break;}
 case 15:case 16:case 17:{taiyin_ziwei_chart* value=0;if(operation==15){const auto patch=bazi_read<taiyin_ziwei_placement_patch>(in);result=taiyin_ziwei_chart_modify(context,chart,&patch,&value);}else if(operation==16)result=taiyin_ziwei_chart_shift_life_palace(chart,web_integer<int32_t>(in.next()),&value);else result=taiyin_ziwei_chart_reset(chart,&value);out.push_back(reinterpret_cast<uintptr_t>(value));break;}
 case 18:case 19:{const auto jd=bazi_read<taiyin_split_julian_date>(in);taiyin_calendar_datetime time={};taiyin_ziwei_chart_clock clock={};if(operation==18)eclipse_read(time,in);else eclipse_read(clock,in);const auto options=bazi_read<taiyin_ziwei_flow_options>(in);const int level=web_integer<int>(in.next());taiyin_ziwei_flow_summary summary;taiyin_ziwei_flow_summary_init(&summary);result=operation==18?taiyin_ziwei_chart_set_flow(context,calendar,&jd,&time,&options,level,chart,&summary,&diag):taiyin_ziwei_chart_set_flow_at_ut1(context,calendar,&jd,&clock,&options,level,chart,&summary,&diag);eclipse_pack(summary,out);break;}
 case 20:result=taiyin_ziwei_chart_truncate_flow(chart,web_integer<int>(in.next()));break;
 case 21:out.push_back(taiyin_ziwei_chart_flow_layer_count(chart));break;
 case 22:{const int level=web_integer<int>(in.next());uint8_t life=0,stem=0,branch=0;taiyin_ziwei_transform_set transforms;taiyin_ziwei_transform_set_init(&transforms);result=taiyin_ziwei_chart_get_flow_layer_summary(chart,level,&life,&stem,&branch);if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_chart_get_flow_transforms(chart,level,&transforms);out.push_back(life);out.push_back(stem);out.push_back(branch);eclipse_pack(transforms,out);break;}
 case 23:{const int level=web_integer<int>(in.next());const uint16_t id=web_integer<uint16_t>(in.next());uint8_t pos=255;result=taiyin_ziwei_chart_get_flow_star_position(chart,level,id,&pos);out.push_back(pos);break;}
 case 24:{const int level=web_integer<int>(in.next());const uint8_t branch=web_integer<uint8_t>(in.next());result=ziwei_ids(*reply,[&](uint16_t* ids,size_t capacity,size_t* count){return taiyin_ziwei_chart_get_flow_palace_stars(chart,level,branch,ids,capacity,count);});break;}
 case 25:case 26:{const auto clock=bazi_read<taiyin_ziwei_chart_clock>(in);if(operation==25){const auto jd=bazi_read<taiyin_split_julian_date>(in);taiyin_calendar_datetime time;taiyin_calendar_datetime_init(&time);result=taiyin_ziwei_chart_time_from_ut1(calendar,&clock,&jd,&time,&diag);eclipse_pack(time,out);}else{const auto time=bazi_read<taiyin_calendar_datetime>(in);taiyin_split_julian_date jd={};result=taiyin_ziwei_chart_time_to_ut1(calendar,&clock,&time,&jd,&diag);eclipse_pack(jd,out);}break;}
 case 27:case 28:{const auto clock=bazi_read<taiyin_ziwei_chart_clock>(in);const auto jd=bazi_read<taiyin_split_julian_date>(in);const int mode=web_integer<int>(in.next()),direction=web_integer<int>(in.next());taiyin_split_julian_date target={};taiyin_calendar_datetime time;taiyin_calendar_datetime_init(&time);uint8_t segment=0;result=operation==27?taiyin_ziwei_step_flow_hour_at_ut1(calendar,&clock,&jd,mode,direction,&target,&time,&segment,&diag):taiyin_ziwei_step_flow_day_at_ut1(calendar,&clock,&jd,direction,&target,&time,&diag);eclipse_pack(target,out);eclipse_pack(time,out);out.push_back(segment);break;}
 case 29:case 30:{const auto jd=bazi_read<taiyin_split_julian_date>(in);const auto time=bazi_read<taiyin_calendar_datetime>(in);const int mode=web_integer<int>(in.next()),direction=web_integer<int>(in.next());taiyin_split_julian_date target={};taiyin_calendar_datetime target_time;taiyin_calendar_datetime_init(&target_time);uint8_t segment=0;result=operation==29?taiyin_ziwei_step_flow_hour_target(&jd,&time,mode,direction,&target,&target_time,&segment):taiyin_ziwei_step_flow_day_target(&jd,&time,direction,&target,&target_time);eclipse_pack(target,out);eclipse_pack(target_time,out);out.push_back(segment);break;}
 case 31:case 32:{const auto request=bazi_read<taiyin_ziwei_reverse_request>(in);taiyin_ziwei_chart_clock clock={};if(operation==32)eclipse_read(clock,in);result=ziwei_array<taiyin_ziwei_reverse_candidate>(*reply,[&](taiyin_ziwei_reverse_candidate* values,size_t capacity,size_t* count){return operation==31?taiyin_ziwei_reverse_lookup_tier1(context,calendar,&request,values,capacity,count,&diag):taiyin_ziwei_reverse_lookup_tier1_at_ut1(context,calendar,&request,&clock,values,capacity,count,&diag);});break;}
 case 33:case 34:case 35:case 36:{const auto options=bazi_read<taiyin_ziwei_casting_options>(in);taiyin_ziwei_casting_chart* value=0;if(operation==33){const auto data=bazi_read<taiyin_ziwei_placement_input>(in);result=taiyin_ziwei_casting_chart_create(context,&data,&options,&value);}else if(operation==34)result=taiyin_ziwei_casting_chart_from_index(context,web_integer<uint32_t>(in.next()),&options,&value);else if(operation==35){const auto number=bazi_text(in);result=taiyin_ziwei_casting_chart_from_number(context,number.c_str(),&options,&value);}else result=taiyin_ziwei_casting_chart_random(context,&options,0,0,&value);out.push_back(reinterpret_cast<uintptr_t>(value));break;}
 case 37:taiyin_ziwei_casting_chart_destroy(casting);break;
 case 38:{taiyin_ziwei_casting_summary summary;taiyin_ziwei_casting_summary_init(&summary);result=taiyin_ziwei_casting_chart_get_summary(casting,&summary);eclipse_pack(summary,out);size_t size=0;if(taiyin_call_result_status(result)==0)result=taiyin_ziwei_casting_chart_get_number(casting,0,0,&size);if(taiyin_call_result_status(result)==0){std::vector<char> text(size);result=taiyin_ziwei_casting_chart_get_number(casting,text.data(),size,&size);bazi_string(out,text.data());}break;}
 case 39:{size_t count=0;result=taiyin_ziwei_casting_chart_get_stars(casting,0,0,0,&count);if(taiyin_call_result_status(result)==0){std::vector<uint8_t> positions(count);std::vector<uint16_t> masks(count);const size_t capacity=count;result=taiyin_ziwei_casting_chart_get_stars(casting,positions.data(),masks.data(),capacity,&count);if(count>capacity)throw std::runtime_error("invalid star count");reply->values[2]=count;for(size_t i=0;i<count;++i){out.push_back(positions[i]);out.push_back(masks[i]);}}break;}
 case 40:{const uint16_t id=web_integer<uint16_t>(in.next());int32_t brightness=-1;result=taiyin_ziwei_casting_chart_get_brightness(context,casting,id,&brightness);out.push_back(brightness);break;}
 case 41:case 45:result=ziwei_omitted(*reply,[&](taiyin_ziwei_omitted_placement* values,size_t capacity,size_t* count){return operation==41?taiyin_ziwei_casting_chart_get_omitted_placements(casting,values,capacity,count):taiyin_ziwei_chart_get_omitted_placements(chart,values,capacity,count);});break;
 case 42:case 43:case 44:{taiyin_ziwei_casting_chart* value=0;if(operation==42){const auto patch=bazi_read<taiyin_ziwei_placement_patch>(in);result=taiyin_ziwei_casting_chart_modify(context,casting,&patch,&value);}else if(operation==43)result=taiyin_ziwei_casting_chart_shift_life_palace(casting,web_integer<int32_t>(in.next()),&value);else result=taiyin_ziwei_casting_chart_reset(casting,&value);out.push_back(reinterpret_cast<uintptr_t>(value));break;}
 default:result=taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT,0);break;
 }
 if(in.cursor!=in.count)throw std::invalid_argument("extra Ziwei input");diagnostic(diag,out.data());out[0]=taiyin_call_result_flags(result);out[1]=taiyin_call_result_status(result);return reply.release();
 }catch(const std::bad_alloc&){return 0;}catch(const std::invalid_argument&){return web_error_reply(TAIYIN_ERROR_INVALID_ARGUMENT);}catch(...){return web_error_reply(TAIYIN_ERROR_INTERNAL);}
}
}
