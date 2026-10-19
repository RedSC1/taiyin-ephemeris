// Variable-size stellar/observed calls and source strings. Algorithms stay in C ABI.
extern "C" {
WebEclipseReply* taiyin_web_astronomy_extra(taiyin_context* context,int operation,
                                           const double* input,size_t input_count) {
 try {
  std::unique_ptr<WebEclipseReply> reply(new WebEclipseReply());
  AstrReader in={input,input_count,0};
  taiyin_call_result result=taiyin_make_call_result(0,0);
  if(operation==5){if(input_count)throw std::invalid_argument("unexpected capability input");astr_pack_u64(taiyin_get_capabilities(),reply->values);return reply.release();}
  if(operation==4) {
   if(input_count)throw std::invalid_argument("unexpected metadata input");
   const size_t count=taiyin_runtime_registered_data_source_count();
   if(count>100000)throw std::invalid_argument("too many sources");
   reply->values[2]=count;
   uint32_t flags=0;
   for(size_t i=0;i<count;++i) {
    taiyin_runtime_registered_data_source source;taiyin_runtime_registered_data_source_init(&source);
    size_t required=0;
    result=taiyin_runtime_get_registered_data_source(i,&source,nullptr,0,&required);
    flags|=taiyin_call_result_flags(result);
    if(taiyin_call_result_status(result)!=0)break;
    if(required<1||required>1048576)throw std::invalid_argument("source name too long");
    std::vector<char> name(required,0);
    result=taiyin_runtime_get_registered_data_source(i,&source,name.data(),name.size(),&required);
    flags|=taiyin_call_result_flags(result);
    if(taiyin_call_result_status(result)!=0)break;
    if(required>name.size()||name.back()!=0)throw std::runtime_error("invalid source name");
    astr_pack(source,reply->values);astr_text_out(std::string(name.data()),reply->values);
   }
   reply->values[0]=flags;reply->values[1]=taiyin_call_result_status(result);return reply.release();
  }
  if(operation<0||operation>3)throw std::invalid_argument("unknown variable astronomy operation");
  const int mode=operation==0?astr_scalar<uint32_t>(in):0;
  taiyin_split_julian_date jd={0,0},tt={0,0};taiyin_calendar_datetime utc;
  taiyin_calendar_datetime_init(&utc);
  if(operation==2)astr_read(utc,in);else astr_read(jd,in);
  double delta_t=0;
  if(operation==0&&mode==0)astr_read(tt,in);
  if(operation==0&&mode==3)delta_t=astr_scalar<double>(in);
  const uint64_t flags=astr_u64(in);
  const size_t count=astr_scalar<uint32_t>(in);
  if(count<1||count>(operation==1||operation==2?10u:10000u))throw std::invalid_argument("invalid astronomy batch size");
  std::vector<std::string> keys;std::vector<const char*> key_ptrs;std::vector<int32_t> ids;
  if(operation==0||operation==3) {
   keys.reserve(count);key_ptrs.reserve(count);
   for(size_t i=0;i<count;++i)keys.push_back(astr_text(in));
   for(size_t i=0;i<count;++i)key_ptrs.push_back(keys[i].c_str());
  } else {ids.resize(count);for(size_t i=0;i<count;++i)ids[i]=astr_scalar<int32_t>(in);}
  if(in.cursor!=in.count)throw std::invalid_argument("trailing variable astronomy input");
  std::vector<taiyin_ephemeris_diagnostic> diags(count);
  for(size_t i=0;i<count;++i)taiyin_ephemeris_diagnostic_init(&diags[i]);
  std::vector<double> positions(count*6,0);
  std::vector<taiyin_observed_position> observed(count);
  for(size_t i=0;i<count;++i)taiyin_observed_position_init(&observed[i]);
  if(operation==0) {
   if(flags>UINT32_MAX)throw std::invalid_argument("stellar position flags exceed uint32");
   if(count==1) {
    if(mode==0)result=taiyin_calc_star_position_tdb(context,key_ptrs[0],&jd,&tt,flags,positions.data(),diags.data());
    else if(mode==1)result=taiyin_calc_star_position_tt(context,key_ptrs[0],&jd,flags,positions.data(),diags.data());
    else if(mode==2)result=taiyin_calc_star_position_ut(context,key_ptrs[0],&jd,flags,positions.data(),diags.data());
    else if(mode==3)result=taiyin_calc_star_position_ut_delta_t(context,key_ptrs[0],&jd,delta_t,flags,positions.data(),diags.data());
    else throw std::invalid_argument("unknown stellar time mode");
   } else {
    if(mode==0)result=taiyin_calc_star_positions_tdb(context,key_ptrs.data(),count,&jd,&tt,flags,positions.data(),diags.data());
    else if(mode==1)result=taiyin_calc_star_positions_tt(context,key_ptrs.data(),count,&jd,flags,positions.data(),diags.data());
    else if(mode==2)result=taiyin_calc_star_positions_ut(context,key_ptrs.data(),count,&jd,flags,positions.data(),diags.data());
    else if(mode==3)result=taiyin_calc_star_positions_ut_delta_t(context,key_ptrs.data(),count,&jd,delta_t,flags,positions.data(),diags.data());
    else throw std::invalid_argument("unknown stellar time mode");
   }
  } else if(operation==1)result=taiyin_calc_observed_bodies_ut(context,&jd,ids.data(),count,flags,observed.data(),diags.data());
  else if(operation==2)result=taiyin_calc_observed_bodies_utc(context,&utc,ids.data(),count,flags,observed.data(),diags.data());
  else if(count==1)result=taiyin_calc_observed_star_ut(context,key_ptrs[0],&jd,flags,observed.data(),diags.data());
  else result=taiyin_calc_observed_stars_ut(context,key_ptrs.data(),count,&jd,flags,observed.data(),diags.data());
  reply->values[2]=count;
  size_t failing=0;
  for(size_t i=0;i<count;++i)if(diags[i].status==taiyin_call_result_status(result)){failing=i;break;}
  diagnostic(diags[failing],reply->values.data());
  for(size_t i=0;i<count;++i){const size_t offset=reply->values.size();reply->values.resize(offset+32,0);diagnostic(diags[i],reply->values.data()+offset);}
  if(operation==0)reply->values.insert(reply->values.end(),positions.begin(),positions.end());
  else for(size_t i=0;i<count;++i)astr_pack(observed[i],reply->values);
  reply->values[0]=taiyin_call_result_flags(result);reply->values[1]=taiyin_call_result_status(result);
  return reply.release();
 }catch(const std::bad_alloc&){return web_error_reply(TAIYIN_ERROR_OUT_OF_MEMORY);}
 catch(const std::invalid_argument&){return web_error_reply(TAIYIN_ERROR_INVALID_ARGUMENT);}
 catch(...){return web_error_reply(TAIYIN_ERROR_INTERNAL);}
}
}
