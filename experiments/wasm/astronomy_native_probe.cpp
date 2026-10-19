// Opt-in native oracle for the exact same field-wise Web astronomy adapter.
#include "taiyin/c/taiyin.h"
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>
namespace {
struct WebEclipseReply {std::vector<double> values;WebEclipseReply():values(32,0){}};
WebEclipseReply* web_error_reply(int status) noexcept {try{auto* r=new WebEclipseReply();r->values[1]=status;return r;}catch(...){return nullptr;}}
void diagnostic(const taiyin_ephemeris_diagnostic& d,double* out) {
 out[10]=d.status;out[11]=d.target_id;out[12]=d.center_id;out[13]=d.frame;
 out[14]=double(d.jd_tdb.day_number);out[15]=d.jd_tdb.day_fraction;
 out[16]=d.candidate_count;out[17]=d.attempted_method_id;
 out[18]=d.nearest_coverage_start;out[19]=d.nearest_coverage_end;
 out[20]=d.component_target_id;out[21]=d.component_center_id;out[22]=d.component_method_id;
 out[23]=d.time_scale_route;out[24]=d.time_scale_fallback_reason;out[25]=d.time_scale_flags;
 out[26]=d.tai_minus_utc_seconds;out[27]=d.dut1_seconds;out[28]=d.delta_t_seconds;
}
}
#include "astronomy_bridge.generated.inc"
#include "astronomy_extra.inc"
int main(int argc,char** argv) {
 taiyin_runtime_config config;taiyin_runtime_config_init(&config);
 config.load_packaged_data=1;config.load_builtin_eop=1;
 if(taiyin_call_result_status(taiyin_runtime_initialize(&config))!=0)return 1;
 if(argc>1&&taiyin_call_result_status(taiyin_star_catalog_add_tsf1(argv[1]))!=0)return 2;
 taiyin_context* context=nullptr;
 if(taiyin_call_result_status(taiyin_context_create(&context))!=0)return 3;
 taiyin_observer_location observer;taiyin_observer_location_init(&observer);
 observer.longitude_deg=118.582;observer.latitude_deg=37.449;observer.height_m=80;
 taiyin_context_set_observer_location(context,&observer);
 taiyin_context_set_standard_atmosphere(context);
 std::cout<<std::setprecision(17);
 std::string line;
 while(std::getline(std::cin,line)) {
  std::istringstream stream(line);int domain=0,op=0;size_t n=0;stream>>domain>>op>>n;
  if(!stream||n>1000000)return 4;
  if(domain==2){taiyin_context_reset(context);taiyin_context_set_observer_location(context,&observer);taiyin_context_set_standard_atmosphere(context);continue;}
  std::vector<double> input;input.reserve(n);
  for(size_t i=0;i<n;++i){std::string value;stream>>value;if(!stream)return 5;input.push_back(std::stod(value));}
  std::unique_ptr<WebEclipseReply> reply(domain==0?taiyin_web_astronomy(context,op,input.data(),input.size()):taiyin_web_astronomy_extra(context,op,input.data(),input.size()));
  if(!reply)return 6;
  std::cout<<reply->values.size();for(double v:reply->values)std::cout<<' '<<v;std::cout<<'\n';
 }
 taiyin_context_destroy(context);return 0;
}
