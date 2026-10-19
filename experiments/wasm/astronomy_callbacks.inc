// Synchronous language callbacks; no borrowed taiyin_context* crosses into JS.
#include <map>
EM_JS(int32_t, web_astronomy_js_callback,
      (int kind,int token,const double* input,int count,double* output,int capacity), {
 return Module['taiyinAstronomyCallback'](kind,token,input,count,output,capacity);
});
namespace {
// Wasm calls are synchronous/serialized. Keep target ownership in this bridge,
// since the native position API unregisters by ID rather than by owner token.
std::map<int32_t, uint64_t> web_target_registrations;
uint64_t web_next_target_registration = 0;
void web_clear_target_registrations() { web_target_registrations.clear(); }
void web_callback_diagnostic(taiyin_ephemeris_diagnostic* diagnostic,
 taiyin_status status,int32_t target,const taiyin_split_julian_date* tdb) {
 if(!diagnostic)return;
 taiyin_ephemeris_diagnostic_init(diagnostic);
 diagnostic->status=status;diagnostic->target_id=target;diagnostic->jd_tdb=*tdb;
}
taiyin_status web_custom_position(const taiyin_context*,int32_t target,
 const taiyin_split_julian_date* tdb,const taiyin_split_julian_date* tt,
 uint32_t flags,double* out,taiyin_ephemeris_diagnostic* diagnostic,void* token) {
 const double input[]={double(target),double(tdb->day_number),tdb->day_fraction,double(tt->day_number),tt->day_fraction,double(flags)};
 const taiyin_status status=web_astronomy_js_callback(0,int(reinterpret_cast<uintptr_t>(token)),input,6,out,6);
 web_callback_diagnostic(diagnostic,status,target,tdb);
 return status;
}
taiyin_status web_custom_state(const taiyin_context*,int32_t target,
 const taiyin_split_julian_date* tdb,const taiyin_split_julian_date* tt,
 uint32_t flags,taiyin_cartesian_state* out,taiyin_ephemeris_diagnostic* diagnostic,void* token) {
 const double input[]={double(target),double(tdb->day_number),tdb->day_fraction,double(tt->day_number),tt->day_fraction,double(flags)};
 double value[9]={};const int status=web_astronomy_js_callback(1,int(reinterpret_cast<uintptr_t>(token)),input,6,value,9);
 if(status==0){taiyin_cartesian_state_init(out);out->position_au={value[0],value[1],value[2]};out->velocity_au_per_day={value[3],value[4],value[5]};out->acceleration_au_per_day2={value[6],value[7],value[8]};}
 web_callback_diagnostic(diagnostic,status,target,tdb);
 return status;
}
taiyin_status web_custom_ayanamsha(const taiyin_context*,const taiyin_split_julian_date* tt,
 uint64_t flags,double* out,void* token) {
 const double input[]={double(tt->day_number),tt->day_fraction,double(uint32_t(flags)),double(uint32_t(flags>>32))};
 return web_astronomy_js_callback(2,int(reinterpret_cast<uintptr_t>(token)),input,4,out,1);
}
taiyin_bool web_custom_houses(const taiyin_house_system_dispatch_data* d,double* out,void* token) {
 const double input[]={d->armc_rad,d->observer_latitude_rad,d->true_obliquity_rad,d->ascendant_rad,d->midheaven_rad};
 const int status=web_astronomy_js_callback(3,int(reinterpret_cast<uintptr_t>(token)),input,5,out,12);
 return status==0?1:0;
}
}
extern "C" {
WebEclipseReply* taiyin_web_astronomy_callbacks(int operation,const double* input,size_t input_count) {
 try{
  std::unique_ptr<WebEclipseReply> reply(new WebEclipseReply());AstrReader in={input,input_count,0};
  reply->values.reserve(34); // No allocating reply write after registration.
  const int kind=astr_scalar<uint32_t>(in),id=astr_scalar<int32_t>(in);
  taiyin_call_result code=taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT,0);
  uint64_t registration=0;
  if(operation==0){
   const int token=astr_scalar<int32_t>(in),model=astr_scalar<int32_t>(in),has_state=astr_scalar<uint8_t>(in);
   if(token<1||in.cursor!=in.count)throw std::invalid_argument("invalid callback registration");
   void* user=reinterpret_cast<void*>(static_cast<uintptr_t>(token));
   if(kind==0){
    // Allocate bookkeeping before registering. Never recycle a generation,
    // including across runtime resets, so a stale handle cannot gain ownership.
    if(web_next_target_registration==UINT64_MAX)throw std::invalid_argument("callback generation exhausted");
    const auto inserted=web_target_registrations.emplace(id,++web_next_target_registration);
    if(inserted.second){
     code=taiyin_register_native_position_evaluator(id,web_custom_position,has_state?web_custom_state:nullptr,user);
     if(taiyin_call_result_status(code)==0)registration=inserted.first->second;
     else web_target_registrations.erase(inserted.first);
    }
   }
   else if(kind==1)code=taiyin_register_ayanamsha_model_with_token(id,web_custom_ayanamsha,model,user,&registration);
   else if(kind==2)code=taiyin_register_house_system_model_with_token(id,web_custom_houses,model,user,&registration);
   astr_pack_u64(registration,reply->values);
  }else if(operation==1){
   registration=astr_u64(in);if(in.cursor!=in.count)throw std::invalid_argument("trailing callback input");
   if(kind==0){
    const auto owner=web_target_registrations.find(id);
    if(registration!=0&&owner!=web_target_registrations.end()&&owner->second==registration){
     code=taiyin_unregister_native_position_evaluator(id);
     if(taiyin_call_result_status(code)==0)web_target_registrations.erase(owner);
    }
   }
   else if(kind==1)code=taiyin_unregister_ayanamsha_model_with_token(id,registration);
   else if(kind==2)code=taiyin_unregister_house_system_model_with_token(id,registration);
  }
  reply->values[0]=taiyin_call_result_flags(code);reply->values[1]=taiyin_call_result_status(code);return reply.release();
 }catch(const std::bad_alloc&){return web_error_reply(TAIYIN_ERROR_OUT_OF_MEMORY);}
 catch(const std::invalid_argument&){return web_error_reply(TAIYIN_ERROR_INVALID_ARGUMENT);}
 catch(...){return web_error_reply(TAIYIN_ERROR_INTERNAL);}
}
}
