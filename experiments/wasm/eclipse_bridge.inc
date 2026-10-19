// Field-wise eclipse transport. Native struct layout never crosses JS.
#include <stdexcept>
#include <memory>
#include "transport_internal.h"
namespace {
template<class T> void eclipse_zero_init(T* v) { *v=T{}; }
struct WebEclipseReply { std::vector<double> values; WebEclipseReply(): values(32, 0) {} };
WebEclipseReply* web_error_reply(int status) noexcept {
 try {std::unique_ptr<WebEclipseReply> reply(new WebEclipseReply());reply->values[1]=status;return reply.release();}
 catch(...) {return 0;}
}
struct EclipseReader { const double* input; size_t count, cursor; double next() { if(cursor>=count) throw std::invalid_argument("short eclipse input"); return input[cursor++]; } };
void eclipse_pack(const taiyin_split_julian_date& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.day_number));
 out.push_back(static_cast<double>(v.day_fraction));
}
void eclipse_read(taiyin_split_julian_date& v, EclipseReader& in) {
 v.day_number=web_jd_day(in.next());
 v.day_fraction=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_lunar_eclipse_result_tt& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.kind));
 eclipse_pack(v.maximum_jd_tt, out);
 out.push_back(static_cast<double>(v.umbral_magnitude));
 out.push_back(static_cast<double>(v.penumbral_magnitude));
 out.push_back(static_cast<double>(v.axis_distance_rad));
 out.push_back(static_cast<double>(v.umbra_radius_rad));
 out.push_back(static_cast<double>(v.penumbra_radius_rad));
 out.push_back(static_cast<double>(v.moon_radius_rad));
 for(size_t i=0;i<7;++i) eclipse_pack(v.contact_jd_tt[i], out);
}
void eclipse_read(taiyin_lunar_eclipse_result_tt& v, EclipseReader& in) {
 taiyin_lunar_eclipse_result_tt_init(&v);
 v.kind=web_integer<uint32_t>(in.next());
 eclipse_read(v.maximum_jd_tt, in);
 v.umbral_magnitude=static_cast<double>(in.next());
 v.penumbral_magnitude=static_cast<double>(in.next());
 v.axis_distance_rad=static_cast<double>(in.next());
 v.umbra_radius_rad=static_cast<double>(in.next());
 v.penumbra_radius_rad=static_cast<double>(in.next());
 v.moon_radius_rad=static_cast<double>(in.next());
 for(size_t i=0;i<7;++i) eclipse_read(v.contact_jd_tt[i], in);
}
void eclipse_pack(const taiyin_lunar_eclipse_result_ut& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.kind));
 eclipse_pack(v.maximum_jd_ut, out);
 out.push_back(static_cast<double>(v.delta_t_seconds));
 out.push_back(static_cast<double>(v.umbral_magnitude));
 out.push_back(static_cast<double>(v.penumbral_magnitude));
 out.push_back(static_cast<double>(v.axis_distance_rad));
 out.push_back(static_cast<double>(v.umbra_radius_rad));
 out.push_back(static_cast<double>(v.penumbra_radius_rad));
 out.push_back(static_cast<double>(v.moon_radius_rad));
 for(size_t i=0;i<7;++i) eclipse_pack(v.contact_jd_ut[i], out);
}
void eclipse_read(taiyin_lunar_eclipse_result_ut& v, EclipseReader& in) {
 taiyin_lunar_eclipse_result_ut_init(&v);
 v.kind=web_integer<uint32_t>(in.next());
 eclipse_read(v.maximum_jd_ut, in);
 v.delta_t_seconds=static_cast<double>(in.next());
 v.umbral_magnitude=static_cast<double>(in.next());
 v.penumbral_magnitude=static_cast<double>(in.next());
 v.axis_distance_rad=static_cast<double>(in.next());
 v.umbra_radius_rad=static_cast<double>(in.next());
 v.penumbra_radius_rad=static_cast<double>(in.next());
 v.moon_radius_rad=static_cast<double>(in.next());
 for(size_t i=0;i<7;++i) eclipse_read(v.contact_jd_ut[i], in);
}
void eclipse_pack(const taiyin_local_lunar_eclipse_result_tt& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.eclipse_kind));
 out.push_back(static_cast<double>(v.visibility_flags));
 eclipse_pack(v.maximum_jd_tt, out);
 out.push_back(static_cast<double>(v.umbral_magnitude));
 out.push_back(static_cast<double>(v.penumbral_magnitude));
 for(size_t i=0;i<7;++i) eclipse_pack(v.contact_jd_tt[i], out);
 for(size_t i=0;i<7;++i) out.push_back(static_cast<double>(v.contact_moon_altitude_deg[i]));
 for(size_t i=0;i<7;++i) out.push_back(static_cast<double>(v.contact_moon_azimuth_deg[i]));
 eclipse_pack(v.moonrise_jd_tt, out);
 eclipse_pack(v.moonset_jd_tt, out);
}
void eclipse_read(taiyin_local_lunar_eclipse_result_tt& v, EclipseReader& in) {
 taiyin_local_lunar_eclipse_result_tt_init(&v);
 v.eclipse_kind=web_integer<uint32_t>(in.next());
 v.visibility_flags=web_integer<uint32_t>(in.next());
 eclipse_read(v.maximum_jd_tt, in);
 v.umbral_magnitude=static_cast<double>(in.next());
 v.penumbral_magnitude=static_cast<double>(in.next());
 for(size_t i=0;i<7;++i) eclipse_read(v.contact_jd_tt[i], in);
 for(size_t i=0;i<7;++i) v.contact_moon_altitude_deg[i]=static_cast<double>(in.next());
 for(size_t i=0;i<7;++i) v.contact_moon_azimuth_deg[i]=static_cast<double>(in.next());
 eclipse_read(v.moonrise_jd_tt, in);
 eclipse_read(v.moonset_jd_tt, in);
}
void eclipse_pack(const taiyin_local_lunar_eclipse_result_ut& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.eclipse_kind));
 out.push_back(static_cast<double>(v.visibility_flags));
 eclipse_pack(v.maximum_jd_ut, out);
 out.push_back(static_cast<double>(v.delta_t_seconds));
 out.push_back(static_cast<double>(v.umbral_magnitude));
 out.push_back(static_cast<double>(v.penumbral_magnitude));
 for(size_t i=0;i<7;++i) eclipse_pack(v.contact_jd_ut[i], out);
 for(size_t i=0;i<7;++i) out.push_back(static_cast<double>(v.contact_moon_altitude_deg[i]));
 for(size_t i=0;i<7;++i) out.push_back(static_cast<double>(v.contact_moon_azimuth_deg[i]));
 eclipse_pack(v.moonrise_jd_ut, out);
 eclipse_pack(v.moonset_jd_ut, out);
}
void eclipse_read(taiyin_local_lunar_eclipse_result_ut& v, EclipseReader& in) {
 taiyin_local_lunar_eclipse_result_ut_init(&v);
 v.eclipse_kind=web_integer<uint32_t>(in.next());
 v.visibility_flags=web_integer<uint32_t>(in.next());
 eclipse_read(v.maximum_jd_ut, in);
 v.delta_t_seconds=static_cast<double>(in.next());
 v.umbral_magnitude=static_cast<double>(in.next());
 v.penumbral_magnitude=static_cast<double>(in.next());
 for(size_t i=0;i<7;++i) eclipse_read(v.contact_jd_ut[i], in);
 for(size_t i=0;i<7;++i) v.contact_moon_altitude_deg[i]=static_cast<double>(in.next());
 for(size_t i=0;i<7;++i) v.contact_moon_azimuth_deg[i]=static_cast<double>(in.next());
 eclipse_read(v.moonrise_jd_ut, in);
 eclipse_read(v.moonset_jd_ut, in);
}
void eclipse_pack(const taiyin_solar_eclipse_result_tt& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.kind));
 eclipse_pack(v.maximum_jd_tt, out);
 out.push_back(static_cast<double>(v.axis_distance_km));
 out.push_back(static_cast<double>(v.penumbra_radius_km));
 out.push_back(static_cast<double>(v.core_radius_km));
 out.push_back(static_cast<double>(v.penumbral_margin_km));
 out.push_back(static_cast<double>(v.central_margin_km));
 out.push_back(static_cast<double>(v.maximum_latitude_deg));
 out.push_back(static_cast<double>(v.maximum_longitude_deg));
 for(size_t i=0;i<5;++i) eclipse_pack(v.contact_jd_tt[i], out);
}
void eclipse_read(taiyin_solar_eclipse_result_tt& v, EclipseReader& in) {
 taiyin_solar_eclipse_result_tt_init(&v);
 v.kind=web_integer<uint32_t>(in.next());
 eclipse_read(v.maximum_jd_tt, in);
 v.axis_distance_km=static_cast<double>(in.next());
 v.penumbra_radius_km=static_cast<double>(in.next());
 v.core_radius_km=static_cast<double>(in.next());
 v.penumbral_margin_km=static_cast<double>(in.next());
 v.central_margin_km=static_cast<double>(in.next());
 v.maximum_latitude_deg=static_cast<double>(in.next());
 v.maximum_longitude_deg=static_cast<double>(in.next());
 for(size_t i=0;i<5;++i) eclipse_read(v.contact_jd_tt[i], in);
}
void eclipse_pack(const taiyin_solar_eclipse_result_ut& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.kind));
 eclipse_pack(v.maximum_jd_ut, out);
 out.push_back(static_cast<double>(v.delta_t_seconds));
 out.push_back(static_cast<double>(v.axis_distance_km));
 out.push_back(static_cast<double>(v.penumbra_radius_km));
 out.push_back(static_cast<double>(v.core_radius_km));
 out.push_back(static_cast<double>(v.penumbral_margin_km));
 out.push_back(static_cast<double>(v.central_margin_km));
 out.push_back(static_cast<double>(v.maximum_latitude_deg));
 out.push_back(static_cast<double>(v.maximum_longitude_deg));
 for(size_t i=0;i<5;++i) eclipse_pack(v.contact_jd_ut[i], out);
}
void eclipse_read(taiyin_solar_eclipse_result_ut& v, EclipseReader& in) {
 taiyin_solar_eclipse_result_ut_init(&v);
 v.kind=web_integer<uint32_t>(in.next());
 eclipse_read(v.maximum_jd_ut, in);
 v.delta_t_seconds=static_cast<double>(in.next());
 v.axis_distance_km=static_cast<double>(in.next());
 v.penumbra_radius_km=static_cast<double>(in.next());
 v.core_radius_km=static_cast<double>(in.next());
 v.penumbral_margin_km=static_cast<double>(in.next());
 v.central_margin_km=static_cast<double>(in.next());
 v.maximum_latitude_deg=static_cast<double>(in.next());
 v.maximum_longitude_deg=static_cast<double>(in.next());
 for(size_t i=0;i<5;++i) eclipse_read(v.contact_jd_ut[i], in);
}
void eclipse_pack(const taiyin_local_solar_eclipse_circumstances_tt& v, std::vector<double>& out) {
 eclipse_pack(v.jd_tt, out);
 out.push_back(static_cast<double>(v.magnitude));
 out.push_back(static_cast<double>(v.obscuration));
 out.push_back(static_cast<double>(v.center_separation_deg));
 out.push_back(static_cast<double>(v.sun_angular_radius_deg));
 out.push_back(static_cast<double>(v.moon_angular_radius_deg));
 out.push_back(static_cast<double>(v.sun_altitude_deg));
 out.push_back(static_cast<double>(v.sun_azimuth_deg));
}
void eclipse_read(taiyin_local_solar_eclipse_circumstances_tt& v, EclipseReader& in) {
 taiyin_local_solar_eclipse_circumstances_tt_init(&v);
 eclipse_read(v.jd_tt, in);
 v.magnitude=static_cast<double>(in.next());
 v.obscuration=static_cast<double>(in.next());
 v.center_separation_deg=static_cast<double>(in.next());
 v.sun_angular_radius_deg=static_cast<double>(in.next());
 v.moon_angular_radius_deg=static_cast<double>(in.next());
 v.sun_altitude_deg=static_cast<double>(in.next());
 v.sun_azimuth_deg=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_local_solar_eclipse_circumstances_ut& v, std::vector<double>& out) {
 eclipse_pack(v.jd_ut, out);
 out.push_back(static_cast<double>(v.delta_t_seconds));
 out.push_back(static_cast<double>(v.magnitude));
 out.push_back(static_cast<double>(v.obscuration));
 out.push_back(static_cast<double>(v.center_separation_deg));
 out.push_back(static_cast<double>(v.sun_angular_radius_deg));
 out.push_back(static_cast<double>(v.moon_angular_radius_deg));
 out.push_back(static_cast<double>(v.sun_altitude_deg));
 out.push_back(static_cast<double>(v.sun_azimuth_deg));
}
void eclipse_read(taiyin_local_solar_eclipse_circumstances_ut& v, EclipseReader& in) {
 taiyin_local_solar_eclipse_circumstances_ut_init(&v);
 eclipse_read(v.jd_ut, in);
 v.delta_t_seconds=static_cast<double>(in.next());
 v.magnitude=static_cast<double>(in.next());
 v.obscuration=static_cast<double>(in.next());
 v.center_separation_deg=static_cast<double>(in.next());
 v.sun_angular_radius_deg=static_cast<double>(in.next());
 v.moon_angular_radius_deg=static_cast<double>(in.next());
 v.sun_altitude_deg=static_cast<double>(in.next());
 v.sun_azimuth_deg=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_local_solar_eclipse_result_tt& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.kind));
 eclipse_pack(v.maximum_jd_tt, out);
 out.push_back(static_cast<double>(v.magnitude));
 out.push_back(static_cast<double>(v.obscuration));
 out.push_back(static_cast<double>(v.sun_altitude_deg));
 out.push_back(static_cast<double>(v.sun_azimuth_deg));
 for(size_t i=0;i<5;++i) eclipse_pack(v.contact_jd_tt[i], out);
 out.push_back(static_cast<double>(v.position_angle_c1_deg));
 out.push_back(static_cast<double>(v.position_angle_c4_deg));
 out.push_back(static_cast<double>(v.vertex_angle_c1_deg));
 out.push_back(static_cast<double>(v.vertex_angle_c4_deg));
 out.push_back(static_cast<double>(v.sunrise_magnitude));
 out.push_back(static_cast<double>(v.sunset_magnitude));
 out.push_back(static_cast<double>(v.duration_seconds));
 out.push_back(static_cast<double>(v.moon_sun_radius_ratio));
}
void eclipse_read(taiyin_local_solar_eclipse_result_tt& v, EclipseReader& in) {
 taiyin_local_solar_eclipse_result_tt_init(&v);
 v.kind=web_integer<uint32_t>(in.next());
 eclipse_read(v.maximum_jd_tt, in);
 v.magnitude=static_cast<double>(in.next());
 v.obscuration=static_cast<double>(in.next());
 v.sun_altitude_deg=static_cast<double>(in.next());
 v.sun_azimuth_deg=static_cast<double>(in.next());
 for(size_t i=0;i<5;++i) eclipse_read(v.contact_jd_tt[i], in);
 v.position_angle_c1_deg=static_cast<double>(in.next());
 v.position_angle_c4_deg=static_cast<double>(in.next());
 v.vertex_angle_c1_deg=static_cast<double>(in.next());
 v.vertex_angle_c4_deg=static_cast<double>(in.next());
 v.sunrise_magnitude=static_cast<double>(in.next());
 v.sunset_magnitude=static_cast<double>(in.next());
 v.duration_seconds=static_cast<double>(in.next());
 v.moon_sun_radius_ratio=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_local_solar_eclipse_result_ut& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.kind));
 eclipse_pack(v.maximum_jd_ut, out);
 out.push_back(static_cast<double>(v.delta_t_seconds));
 out.push_back(static_cast<double>(v.magnitude));
 out.push_back(static_cast<double>(v.obscuration));
 out.push_back(static_cast<double>(v.sun_altitude_deg));
 out.push_back(static_cast<double>(v.sun_azimuth_deg));
 for(size_t i=0;i<5;++i) eclipse_pack(v.contact_jd_ut[i], out);
 out.push_back(static_cast<double>(v.position_angle_c1_deg));
 out.push_back(static_cast<double>(v.position_angle_c4_deg));
 out.push_back(static_cast<double>(v.vertex_angle_c1_deg));
 out.push_back(static_cast<double>(v.vertex_angle_c4_deg));
 out.push_back(static_cast<double>(v.sunrise_magnitude));
 out.push_back(static_cast<double>(v.sunset_magnitude));
 out.push_back(static_cast<double>(v.duration_seconds));
 out.push_back(static_cast<double>(v.moon_sun_radius_ratio));
}
void eclipse_read(taiyin_local_solar_eclipse_result_ut& v, EclipseReader& in) {
 taiyin_local_solar_eclipse_result_ut_init(&v);
 v.kind=web_integer<uint32_t>(in.next());
 eclipse_read(v.maximum_jd_ut, in);
 v.delta_t_seconds=static_cast<double>(in.next());
 v.magnitude=static_cast<double>(in.next());
 v.obscuration=static_cast<double>(in.next());
 v.sun_altitude_deg=static_cast<double>(in.next());
 v.sun_azimuth_deg=static_cast<double>(in.next());
 for(size_t i=0;i<5;++i) eclipse_read(v.contact_jd_ut[i], in);
 v.position_angle_c1_deg=static_cast<double>(in.next());
 v.position_angle_c4_deg=static_cast<double>(in.next());
 v.vertex_angle_c1_deg=static_cast<double>(in.next());
 v.vertex_angle_c4_deg=static_cast<double>(in.next());
 v.sunrise_magnitude=static_cast<double>(in.next());
 v.sunset_magnitude=static_cast<double>(in.next());
 v.duration_seconds=static_cast<double>(in.next());
 v.moon_sun_radius_ratio=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_local_solar_eclipse_boundary& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.center_longitude_deg));
 out.push_back(static_cast<double>(v.center_latitude_deg));
 out.push_back(static_cast<double>(v.center_kind));
 out.push_back(static_cast<double>(v.umbra_north_longitude_deg));
 out.push_back(static_cast<double>(v.umbra_north_latitude_deg));
 out.push_back(static_cast<double>(v.umbra_south_longitude_deg));
 out.push_back(static_cast<double>(v.umbra_south_latitude_deg));
 out.push_back(static_cast<double>(v.penumbra_north_longitude_deg));
 out.push_back(static_cast<double>(v.penumbra_north_latitude_deg));
 out.push_back(static_cast<double>(v.penumbra_south_longitude_deg));
 out.push_back(static_cast<double>(v.penumbra_south_latitude_deg));
 out.push_back(static_cast<double>(v.umbra_width_km));
}
void eclipse_read(taiyin_local_solar_eclipse_boundary& v, EclipseReader& in) {
 taiyin_local_solar_eclipse_boundary_init(&v);
 v.center_longitude_deg=static_cast<double>(in.next());
 v.center_latitude_deg=static_cast<double>(in.next());
 v.center_kind=web_integer<uint32_t>(in.next());
 v.umbra_north_longitude_deg=static_cast<double>(in.next());
 v.umbra_north_latitude_deg=static_cast<double>(in.next());
 v.umbra_south_longitude_deg=static_cast<double>(in.next());
 v.umbra_south_latitude_deg=static_cast<double>(in.next());
 v.penumbra_north_longitude_deg=static_cast<double>(in.next());
 v.penumbra_north_latitude_deg=static_cast<double>(in.next());
 v.penumbra_south_longitude_deg=static_cast<double>(in.next());
 v.penumbra_south_latitude_deg=static_cast<double>(in.next());
 v.umbra_width_km=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_solar_eclipse_path_point& v, std::vector<double>& out) {
 eclipse_pack(v.jd_tt, out);
 eclipse_pack(v.jd_ut, out);
 out.push_back(static_cast<double>(v.latitude_deg));
 out.push_back(static_cast<double>(v.longitude_deg));
 out.push_back(static_cast<double>(v.elevation_m));
 out.push_back(static_cast<double>(v.sun_altitude_deg));
 out.push_back(static_cast<double>(v.sun_azimuth_deg));
}
void eclipse_read(taiyin_solar_eclipse_path_point& v, EclipseReader& in) {
 eclipse_zero_init(&v);
 eclipse_read(v.jd_tt, in);
 eclipse_read(v.jd_ut, in);
 v.latitude_deg=static_cast<double>(in.next());
 v.longitude_deg=static_cast<double>(in.next());
 v.elevation_m=static_cast<double>(in.next());
 v.sun_altitude_deg=static_cast<double>(in.next());
 v.sun_azimuth_deg=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_solar_eclipse_route_row& v, std::vector<double>& out) {
 eclipse_pack(v.jd_tt, out);
 eclipse_pack(v.jd_ut, out);
 eclipse_pack(v.center_line, out);
 eclipse_pack(v.penumbral_north_limit, out);
 eclipse_pack(v.penumbral_south_limit, out);
 eclipse_pack(v.north_limit, out);
 eclipse_pack(v.south_limit, out);
 eclipse_pack(v.half_magnitude_north_limit, out);
 eclipse_pack(v.half_magnitude_south_limit, out);
 out.push_back(static_cast<double>(v.path_width_km));
 out.push_back(static_cast<double>(v.duration_seconds));
 out.push_back(static_cast<double>(v.sun_altitude_deg));
 out.push_back(static_cast<double>(v.sun_azimuth_deg));
}
void eclipse_read(taiyin_solar_eclipse_route_row& v, EclipseReader& in) {
 taiyin_solar_eclipse_route_row_init(&v);
 eclipse_read(v.jd_tt, in);
 eclipse_read(v.jd_ut, in);
 eclipse_read(v.center_line, in);
 eclipse_read(v.penumbral_north_limit, in);
 eclipse_read(v.penumbral_south_limit, in);
 eclipse_read(v.north_limit, in);
 eclipse_read(v.south_limit, in);
 eclipse_read(v.half_magnitude_north_limit, in);
 eclipse_read(v.half_magnitude_south_limit, in);
 v.path_width_km=static_cast<double>(in.next());
 v.duration_seconds=static_cast<double>(in.next());
 v.sun_altitude_deg=static_cast<double>(in.next());
 v.sun_azimuth_deg=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_solar_eclipse_where& v, std::vector<double>& out) {
 eclipse_pack(v.jd_tt, out);
 eclipse_pack(v.jd_ut, out);
 eclipse_pack(v.center_line, out);
 eclipse_pack(v.penumbral_north_limit, out);
 eclipse_pack(v.penumbral_south_limit, out);
 eclipse_pack(v.north_limit, out);
 eclipse_pack(v.south_limit, out);
 out.push_back(static_cast<double>(v.magnitude));
 out.push_back(static_cast<double>(v.obscuration));
 out.push_back(static_cast<double>(v.center_separation_deg));
 out.push_back(static_cast<double>(v.sun_angular_radius_deg));
 out.push_back(static_cast<double>(v.moon_angular_radius_deg));
}
void eclipse_read(taiyin_solar_eclipse_where& v, EclipseReader& in) {
 taiyin_solar_eclipse_where_init(&v);
 eclipse_read(v.jd_tt, in);
 eclipse_read(v.jd_ut, in);
 eclipse_read(v.center_line, in);
 eclipse_read(v.penumbral_north_limit, in);
 eclipse_read(v.penumbral_south_limit, in);
 eclipse_read(v.north_limit, in);
 eclipse_read(v.south_limit, in);
 v.magnitude=static_cast<double>(in.next());
 v.obscuration=static_cast<double>(in.next());
 v.center_separation_deg=static_cast<double>(in.next());
 v.sun_angular_radius_deg=static_cast<double>(in.next());
 v.moon_angular_radius_deg=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_solar_eclipse_route_curve_point& v, std::vector<double>& out) {
 eclipse_pack(v.jd_tt, out);
 eclipse_pack(v.jd_ut, out);
 out.push_back(static_cast<double>(v.curve_kind));
 out.push_back(static_cast<double>(v.latitude_deg));
 out.push_back(static_cast<double>(v.longitude_deg));
}
void eclipse_read(taiyin_solar_eclipse_route_curve_point& v, EclipseReader& in) {
 eclipse_zero_init(&v);
 eclipse_read(v.jd_tt, in);
 eclipse_read(v.jd_ut, in);
 v.curve_kind=web_integer<uint32_t>(in.next());
 v.latitude_deg=static_cast<double>(in.next());
 v.longitude_deg=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_solar_eclipse_route_product_point& v, std::vector<double>& out) {
 eclipse_pack(v.jd_tt, out);
 eclipse_pack(v.jd_ut, out);
 out.push_back(static_cast<double>(v.point_kind));
 out.push_back(static_cast<double>(v.source_curve_kind));
 out.push_back(static_cast<double>(v.latitude_deg));
 out.push_back(static_cast<double>(v.longitude_deg));
 out.push_back(static_cast<double>(v.unwrapped_longitude_deg));
}
void eclipse_read(taiyin_solar_eclipse_route_product_point& v, EclipseReader& in) {
 eclipse_zero_init(&v);
 eclipse_read(v.jd_tt, in);
 eclipse_read(v.jd_ut, in);
 v.point_kind=web_integer<uint32_t>(in.next());
 v.source_curve_kind=web_integer<uint32_t>(in.next());
 v.latitude_deg=static_cast<double>(in.next());
 v.longitude_deg=static_cast<double>(in.next());
 v.unwrapped_longitude_deg=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_solar_eclipse_route_product_summary& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.flags));
 out.push_back(static_cast<double>(v.curve_point_count));
 out.push_back(static_cast<double>(v.center_line_count));
 out.push_back(static_cast<double>(v.core_north_count));
 out.push_back(static_cast<double>(v.core_south_count));
 out.push_back(static_cast<double>(v.core_begin_horizon_count));
 out.push_back(static_cast<double>(v.core_end_horizon_count));
 out.push_back(static_cast<double>(v.penumbral_north_count));
 out.push_back(static_cast<double>(v.penumbral_south_count));
 out.push_back(static_cast<double>(v.half_magnitude_north_count));
 out.push_back(static_cast<double>(v.half_magnitude_south_count));
 out.push_back(static_cast<double>(v.core_polygon_point_count));
 out.push_back(static_cast<double>(v.penumbral_polygon_point_count));
 out.push_back(static_cast<double>(v.half_magnitude_polygon_point_count));
 out.push_back(static_cast<double>(v.polygon_point_count));
 out.push_back(static_cast<double>(v.min_latitude_deg));
 out.push_back(static_cast<double>(v.max_latitude_deg));
 out.push_back(static_cast<double>(v.min_unwrapped_longitude_deg));
 out.push_back(static_cast<double>(v.max_unwrapped_longitude_deg));
}
void eclipse_read(taiyin_solar_eclipse_route_product_summary& v, EclipseReader& in) {
 taiyin_solar_eclipse_route_product_summary_init(&v);
 v.flags=web_integer<uint32_t>(in.next());
 v.curve_point_count=web_integer<size_t>(in.next());
 v.center_line_count=web_integer<size_t>(in.next());
 v.core_north_count=web_integer<size_t>(in.next());
 v.core_south_count=web_integer<size_t>(in.next());
 v.core_begin_horizon_count=web_integer<size_t>(in.next());
 v.core_end_horizon_count=web_integer<size_t>(in.next());
 v.penumbral_north_count=web_integer<size_t>(in.next());
 v.penumbral_south_count=web_integer<size_t>(in.next());
 v.half_magnitude_north_count=web_integer<size_t>(in.next());
 v.half_magnitude_south_count=web_integer<size_t>(in.next());
 v.core_polygon_point_count=web_integer<size_t>(in.next());
 v.penumbral_polygon_point_count=web_integer<size_t>(in.next());
 v.half_magnitude_polygon_point_count=web_integer<size_t>(in.next());
 v.polygon_point_count=web_integer<size_t>(in.next());
 v.min_latitude_deg=static_cast<double>(in.next());
 v.max_latitude_deg=static_cast<double>(in.next());
 v.min_unwrapped_longitude_deg=static_cast<double>(in.next());
 v.max_unwrapped_longitude_deg=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_solar_besselian_elements& v, std::vector<double>& out) {
 out.push_back(static_cast<double>(v.t_hours));
 out.push_back(static_cast<double>(v.x));
 out.push_back(static_cast<double>(v.y));
 out.push_back(static_cast<double>(v.zeta));
 out.push_back(static_cast<double>(v.d_deg));
 out.push_back(static_cast<double>(v.mu_deg));
 out.push_back(static_cast<double>(v.l1));
 out.push_back(static_cast<double>(v.l2));
 out.push_back(static_cast<double>(v.f1_deg));
 out.push_back(static_cast<double>(v.f2_deg));
 out.push_back(static_cast<double>(v.tan_f1));
 out.push_back(static_cast<double>(v.tan_f2));
 out.push_back(static_cast<double>(v.gamma));
}
void eclipse_read(taiyin_solar_besselian_elements& v, EclipseReader& in) {
 taiyin_solar_besselian_elements_init(&v);
 v.t_hours=static_cast<double>(in.next());
 v.x=static_cast<double>(in.next());
 v.y=static_cast<double>(in.next());
 v.zeta=static_cast<double>(in.next());
 v.d_deg=static_cast<double>(in.next());
 v.mu_deg=static_cast<double>(in.next());
 v.l1=static_cast<double>(in.next());
 v.l2=static_cast<double>(in.next());
 v.f1_deg=static_cast<double>(in.next());
 v.f2_deg=static_cast<double>(in.next());
 v.tan_f1=static_cast<double>(in.next());
 v.tan_f2=static_cast<double>(in.next());
 v.gamma=static_cast<double>(in.next());
}
void eclipse_pack(const taiyin_solar_besselian_polynomial& v, std::vector<double>& out) {
 eclipse_pack(v.t0_jd_tt, out);
 out.push_back(static_cast<double>(v.span_hours));
 out.push_back(static_cast<double>(v.sample_step_hours));
 out.push_back(static_cast<double>(v.degree));
 for(size_t i=0;i<8;++i) out.push_back(static_cast<double>(v.x[i]));
 for(size_t i=0;i<8;++i) out.push_back(static_cast<double>(v.y[i]));
 for(size_t i=0;i<8;++i) out.push_back(static_cast<double>(v.zeta[i]));
 for(size_t i=0;i<8;++i) out.push_back(static_cast<double>(v.d_deg[i]));
 for(size_t i=0;i<8;++i) out.push_back(static_cast<double>(v.mu_deg[i]));
 for(size_t i=0;i<8;++i) out.push_back(static_cast<double>(v.l1[i]));
 for(size_t i=0;i<8;++i) out.push_back(static_cast<double>(v.l2[i]));
 out.push_back(static_cast<double>(v.f1_deg));
 out.push_back(static_cast<double>(v.f2_deg));
 out.push_back(static_cast<double>(v.tan_f1));
 out.push_back(static_cast<double>(v.tan_f2));
 eclipse_pack(v.center, out);
 eclipse_pack(v.max_residual, out);
}
void eclipse_read(taiyin_solar_besselian_polynomial& v, EclipseReader& in) {
 taiyin_solar_besselian_polynomial_init(&v);
 eclipse_read(v.t0_jd_tt, in);
 v.span_hours=static_cast<double>(in.next());
 v.sample_step_hours=static_cast<double>(in.next());
 v.degree=web_integer<int32_t>(in.next());
 for(size_t i=0;i<8;++i) v.x[i]=static_cast<double>(in.next());
 for(size_t i=0;i<8;++i) v.y[i]=static_cast<double>(in.next());
 for(size_t i=0;i<8;++i) v.zeta[i]=static_cast<double>(in.next());
 for(size_t i=0;i<8;++i) v.d_deg[i]=static_cast<double>(in.next());
 for(size_t i=0;i<8;++i) v.mu_deg[i]=static_cast<double>(in.next());
 for(size_t i=0;i<8;++i) v.l1[i]=static_cast<double>(in.next());
 for(size_t i=0;i<8;++i) v.l2[i]=static_cast<double>(in.next());
 v.f1_deg=static_cast<double>(in.next());
 v.f2_deg=static_cast<double>(in.next());
 v.tan_f1=static_cast<double>(in.next());
 v.tan_f2=static_cast<double>(in.next());
 eclipse_read(v.center, in);
 eclipse_read(v.max_residual, in);
}
}
extern "C" {
WebEclipseReply* taiyin_web_eclipse(const taiyin_context* context, int operation, const double* input, size_t input_count) {
 try {
 std::unique_ptr<WebEclipseReply> reply(new WebEclipseReply());
 EclipseReader in={input,input_count,0};
 taiyin_ephemeris_diagnostic diag; taiyin_ephemeris_diagnostic_init(&diag);
 taiyin_call_result result=taiyin_make_call_result(TAIYIN_ERROR_INVALID_ARGUMENT,0);
 switch(operation) {
case 0: { // taiyin_solve_lunar_eclipse_at_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_lunar_eclipse_result_tt output; taiyin_lunar_eclipse_result_tt_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_solve_lunar_eclipse_at_tt(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 1: { // taiyin_solve_lunar_eclipse_at_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_lunar_eclipse_result_ut output; taiyin_lunar_eclipse_result_ut_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_solve_lunar_eclipse_at_ut(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 2: { // taiyin_search_next_lunar_eclipse_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint32_t arg2=web_integer<uint32_t>(in.next());
const uint64_t low3=web_integer<uint32_t>(in.next());
const uint64_t arg3=low3|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_lunar_eclipse_result_tt output; taiyin_lunar_eclipse_result_tt_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_search_next_lunar_eclipse_tt(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 3: { // taiyin_search_next_lunar_eclipse_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint32_t arg2=web_integer<uint32_t>(in.next());
const uint64_t low3=web_integer<uint32_t>(in.next());
const uint64_t arg3=low3|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_lunar_eclipse_result_ut output; taiyin_lunar_eclipse_result_ut_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_search_next_lunar_eclipse_ut(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 4: { // taiyin_search_lunar_eclipses_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
taiyin_split_julian_date arg2; eclipse_read(arg2,in);
const uint32_t arg3=web_integer<uint32_t>(in.next());
const uint64_t low4=web_integer<uint32_t>(in.next());
const uint64_t arg4=low4|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
std::vector<taiyin_lunar_eclipse_result_tt> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
output.resize(capacity); for(size_t i=0;i<capacity;++i) taiyin_lunar_eclipse_result_tt_init(&output[i]);
result=taiyin_search_lunar_eclipses_tt(context, &arg1, &arg2, arg3, arg4, output.empty()?0:output.data(), capacity, &count, &diag);
if(taiyin_call_result_status(result)==0) {
 if(count>capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 5: { // taiyin_search_lunar_eclipses_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
taiyin_split_julian_date arg2; eclipse_read(arg2,in);
const uint32_t arg3=web_integer<uint32_t>(in.next());
const uint64_t low4=web_integer<uint32_t>(in.next());
const uint64_t arg4=low4|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
std::vector<taiyin_lunar_eclipse_result_ut> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
output.resize(capacity); for(size_t i=0;i<capacity;++i) taiyin_lunar_eclipse_result_ut_init(&output[i]);
result=taiyin_search_lunar_eclipses_ut(context, &arg1, &arg2, arg3, arg4, output.empty()?0:output.data(), capacity, &count, &diag);
if(taiyin_call_result_status(result)==0) {
 if(count>capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 6: { // taiyin_compute_local_lunar_eclipse_visibility_tt
taiyin_lunar_eclipse_result_tt arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_local_lunar_eclipse_result_tt output; taiyin_local_lunar_eclipse_result_tt_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_local_lunar_eclipse_visibility_tt(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 7: { // taiyin_compute_local_lunar_eclipse_visibility_ut
taiyin_lunar_eclipse_result_ut arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_local_lunar_eclipse_result_ut output; taiyin_local_lunar_eclipse_result_ut_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_local_lunar_eclipse_visibility_ut(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 8: { // taiyin_search_next_local_lunar_eclipse_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint32_t arg2=web_integer<uint32_t>(in.next());
const uint64_t low3=web_integer<uint32_t>(in.next());
const uint64_t arg3=low3|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_local_lunar_eclipse_result_tt output; taiyin_local_lunar_eclipse_result_tt_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_search_next_local_lunar_eclipse_tt(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 9: { // taiyin_search_next_local_lunar_eclipse_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint32_t arg2=web_integer<uint32_t>(in.next());
const uint64_t low3=web_integer<uint32_t>(in.next());
const uint64_t arg3=low3|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_local_lunar_eclipse_result_ut output; taiyin_local_lunar_eclipse_result_ut_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_search_next_local_lunar_eclipse_ut(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 10: { // taiyin_solve_solar_eclipse_at_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_solar_eclipse_result_tt output; taiyin_solar_eclipse_result_tt_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_solve_solar_eclipse_at_tt(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 11: { // taiyin_solve_solar_eclipse_at_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_solar_eclipse_result_ut output; taiyin_solar_eclipse_result_ut_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_solve_solar_eclipse_at_ut(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 12: { // taiyin_search_next_solar_eclipse_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint32_t arg2=web_integer<uint32_t>(in.next());
const uint64_t low3=web_integer<uint32_t>(in.next());
const uint64_t arg3=low3|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_solar_eclipse_result_tt output; taiyin_solar_eclipse_result_tt_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_search_next_solar_eclipse_tt(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 13: { // taiyin_search_next_solar_eclipse_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint32_t arg2=web_integer<uint32_t>(in.next());
const uint64_t low3=web_integer<uint32_t>(in.next());
const uint64_t arg3=low3|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_solar_eclipse_result_ut output; taiyin_solar_eclipse_result_ut_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_search_next_solar_eclipse_ut(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 14: { // taiyin_search_solar_eclipses_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
taiyin_split_julian_date arg2; eclipse_read(arg2,in);
const uint32_t arg3=web_integer<uint32_t>(in.next());
const uint64_t low4=web_integer<uint32_t>(in.next());
const uint64_t arg4=low4|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
std::vector<taiyin_solar_eclipse_result_tt> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
output.resize(capacity); for(size_t i=0;i<capacity;++i) taiyin_solar_eclipse_result_tt_init(&output[i]);
result=taiyin_search_solar_eclipses_tt(context, &arg1, &arg2, arg3, arg4, output.empty()?0:output.data(), capacity, &count, &diag);
if(taiyin_call_result_status(result)==0) {
 if(count>capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 15: { // taiyin_search_solar_eclipses_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
taiyin_split_julian_date arg2; eclipse_read(arg2,in);
const uint32_t arg3=web_integer<uint32_t>(in.next());
const uint64_t low4=web_integer<uint32_t>(in.next());
const uint64_t arg4=low4|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
std::vector<taiyin_solar_eclipse_result_ut> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
output.resize(capacity); for(size_t i=0;i<capacity;++i) taiyin_solar_eclipse_result_ut_init(&output[i]);
result=taiyin_search_solar_eclipses_ut(context, &arg1, &arg2, arg3, arg4, output.empty()?0:output.data(), capacity, &count, &diag);
if(taiyin_call_result_status(result)==0) {
 if(count>capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 16: { // taiyin_solve_local_solar_eclipse_at_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_local_solar_eclipse_result_tt output; taiyin_local_solar_eclipse_result_tt_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_solve_local_solar_eclipse_at_tt(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 17: { // taiyin_solve_local_solar_eclipse_at_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_local_solar_eclipse_result_ut output; taiyin_local_solar_eclipse_result_ut_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_solve_local_solar_eclipse_at_ut(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 18: { // taiyin_search_next_local_solar_eclipse_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint32_t arg2=web_integer<uint32_t>(in.next());
const uint64_t low3=web_integer<uint32_t>(in.next());
const uint64_t arg3=low3|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_local_solar_eclipse_result_tt output; taiyin_local_solar_eclipse_result_tt_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_search_next_local_solar_eclipse_tt(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 19: { // taiyin_search_next_local_solar_eclipse_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint32_t arg2=web_integer<uint32_t>(in.next());
const uint64_t low3=web_integer<uint32_t>(in.next());
const uint64_t arg3=low3|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_local_solar_eclipse_result_ut output; taiyin_local_solar_eclipse_result_ut_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_search_next_local_solar_eclipse_ut(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 20: { // taiyin_compute_local_solar_circumstances_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
taiyin_local_solar_eclipse_circumstances_tt output; taiyin_local_solar_eclipse_circumstances_tt_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_local_solar_circumstances_tt(context, &arg1, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 21: { // taiyin_compute_local_solar_circumstances_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
taiyin_local_solar_eclipse_circumstances_ut output; taiyin_local_solar_eclipse_circumstances_ut_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_local_solar_circumstances_ut(context, &arg1, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 22: { // taiyin_compute_solar_besselian_elements_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const double arg2=static_cast<double>(in.next());
taiyin_solar_besselian_elements output; taiyin_solar_besselian_elements_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_solar_besselian_elements_tt(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 23: { // taiyin_compute_solar_besselian_polynomial_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const double arg2=static_cast<double>(in.next());
const double arg3=static_cast<double>(in.next());
const int32_t arg4=web_integer<int32_t>(in.next());
taiyin_solar_besselian_polynomial output; taiyin_solar_besselian_polynomial_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_solar_besselian_polynomial_tt(context, &arg1, arg2, arg3, arg4, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 24: { // taiyin_evaluate_solar_besselian_polynomial
taiyin_solar_besselian_polynomial arg0; eclipse_read(arg0,in);
const double arg1=static_cast<double>(in.next());
taiyin_solar_besselian_elements output; taiyin_solar_besselian_elements_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_evaluate_solar_besselian_polynomial(&arg0, arg1, &output);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 25: { // taiyin_compute_solar_eclipse_route_row_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_solar_eclipse_route_row output; taiyin_solar_eclipse_route_row_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_solar_eclipse_route_row_tt(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 26: { // taiyin_compute_solar_eclipse_route_row_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_solar_eclipse_route_row output; taiyin_solar_eclipse_route_row_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_solar_eclipse_route_row_ut(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 27: { // taiyin_compute_solar_eclipse_where_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_solar_eclipse_where output; taiyin_solar_eclipse_where_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_solar_eclipse_where_tt(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 28: { // taiyin_compute_solar_eclipse_where_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
taiyin_solar_eclipse_where output; taiyin_solar_eclipse_where_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_solar_eclipse_where_ut(context, &arg1, arg2, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 29: { // taiyin_compute_solar_eclipse_route_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
taiyin_split_julian_date arg2; eclipse_read(arg2,in);
const double arg3=static_cast<double>(in.next());
const uint64_t low4=web_integer<uint32_t>(in.next());
const uint64_t arg4=low4|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
std::vector<taiyin_solar_eclipse_route_row> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
output.resize(capacity); for(size_t i=0;i<capacity;++i) taiyin_solar_eclipse_route_row_init(&output[i]);
result=taiyin_compute_solar_eclipse_route_tt(context, &arg1, &arg2, arg3, arg4, output.empty()?0:output.data(), capacity, &count, &diag);
if(taiyin_call_result_status(result)==0) {
 if(count>capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 30: { // taiyin_compute_solar_eclipse_route_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
taiyin_split_julian_date arg2; eclipse_read(arg2,in);
const double arg3=static_cast<double>(in.next());
const uint64_t low4=web_integer<uint32_t>(in.next());
const uint64_t arg4=low4|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
std::vector<taiyin_solar_eclipse_route_row> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
output.resize(capacity); for(size_t i=0;i<capacity;++i) taiyin_solar_eclipse_route_row_init(&output[i]);
result=taiyin_compute_solar_eclipse_route_ut(context, &arg1, &arg2, arg3, arg4, output.empty()?0:output.data(), capacity, &count, &diag);
if(taiyin_call_result_status(result)==0) {
 if(count>capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 31: { // taiyin_compute_solar_eclipse_route_curves_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
const size_t arg3=web_integer<size_t>(in.next());
std::vector<taiyin_solar_eclipse_route_curve_point> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
if(capacity!=0) throw std::invalid_argument("dynamic capacity must be zero");
result=taiyin_compute_solar_eclipse_route_curves_tt(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &diag);
const uint32_t query_flags=taiyin_call_result_flags(result);
if(taiyin_call_result_status(result)!=0) break;
capacity=count; if(capacity>1000000) throw std::invalid_argument("oversized eclipse output");
output.resize(capacity); for(size_t i=0;i<capacity;++i) eclipse_zero_init(&output[i]);
if(capacity!=0) {
result=taiyin_compute_solar_eclipse_route_curves_tt(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &diag);
}
result=taiyin_make_call_result(taiyin_call_result_status(result), query_flags|taiyin_call_result_flags(result));
if(taiyin_call_result_status(result)==0) {
 if(count>capacity||count!=capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 32: { // taiyin_compute_solar_eclipse_route_curves_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
const size_t arg3=web_integer<size_t>(in.next());
std::vector<taiyin_solar_eclipse_route_curve_point> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
if(capacity!=0) throw std::invalid_argument("dynamic capacity must be zero");
result=taiyin_compute_solar_eclipse_route_curves_ut(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &diag);
const uint32_t query_flags=taiyin_call_result_flags(result);
if(taiyin_call_result_status(result)!=0) break;
capacity=count; if(capacity>1000000) throw std::invalid_argument("oversized eclipse output");
output.resize(capacity); for(size_t i=0;i<capacity;++i) eclipse_zero_init(&output[i]);
if(capacity!=0) {
result=taiyin_compute_solar_eclipse_route_curves_ut(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &diag);
}
result=taiyin_make_call_result(taiyin_call_result_status(result), query_flags|taiyin_call_result_flags(result));
if(taiyin_call_result_status(result)==0) {
 if(count>capacity||count!=capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 33: { // taiyin_compute_solar_eclipse_route_product_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
const size_t arg3=web_integer<size_t>(in.next());
std::vector<taiyin_solar_eclipse_route_product_point> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
taiyin_solar_eclipse_route_product_summary summary; taiyin_solar_eclipse_route_product_summary_init(&summary);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
if(capacity!=0) throw std::invalid_argument("dynamic capacity must be zero");
result=taiyin_compute_solar_eclipse_route_product_tt(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &summary, &diag);
const uint32_t query_flags=taiyin_call_result_flags(result);
if(taiyin_call_result_status(result)!=0) break;
capacity=count; if(capacity>1000000) throw std::invalid_argument("oversized eclipse output");
output.resize(capacity); for(size_t i=0;i<capacity;++i) eclipse_zero_init(&output[i]);
if(capacity!=0) {
result=taiyin_compute_solar_eclipse_route_product_tt(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &summary, &diag);
}
result=taiyin_make_call_result(taiyin_call_result_status(result), query_flags|taiyin_call_result_flags(result));
if(taiyin_call_result_status(result)==0) {
 if(count>capacity||count!=capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
eclipse_pack(summary,reply->values);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 34: { // taiyin_compute_solar_eclipse_route_product_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
const size_t arg3=web_integer<size_t>(in.next());
std::vector<taiyin_solar_eclipse_route_product_point> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
taiyin_solar_eclipse_route_product_summary summary; taiyin_solar_eclipse_route_product_summary_init(&summary);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
if(capacity!=0) throw std::invalid_argument("dynamic capacity must be zero");
result=taiyin_compute_solar_eclipse_route_product_ut(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &summary, &diag);
const uint32_t query_flags=taiyin_call_result_flags(result);
if(taiyin_call_result_status(result)!=0) break;
capacity=count; if(capacity>1000000) throw std::invalid_argument("oversized eclipse output");
output.resize(capacity); for(size_t i=0;i<capacity;++i) eclipse_zero_init(&output[i]);
if(capacity!=0) {
result=taiyin_compute_solar_eclipse_route_product_ut(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &summary, &diag);
}
result=taiyin_make_call_result(taiyin_call_result_status(result), query_flags|taiyin_call_result_flags(result));
if(taiyin_call_result_status(result)==0) {
 if(count>capacity||count!=capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
eclipse_pack(summary,reply->values);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 35: { // taiyin_compute_solar_eclipse_route_map_product_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
const size_t arg3=web_integer<size_t>(in.next());
std::vector<taiyin_solar_eclipse_route_product_point> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
taiyin_solar_eclipse_route_product_summary summary; taiyin_solar_eclipse_route_product_summary_init(&summary);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
if(capacity!=0) throw std::invalid_argument("dynamic capacity must be zero");
result=taiyin_compute_solar_eclipse_route_map_product_tt(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &summary, &diag);
const uint32_t query_flags=taiyin_call_result_flags(result);
if(taiyin_call_result_status(result)!=0) break;
capacity=count; if(capacity>1000000) throw std::invalid_argument("oversized eclipse output");
output.resize(capacity); for(size_t i=0;i<capacity;++i) eclipse_zero_init(&output[i]);
if(capacity!=0) {
result=taiyin_compute_solar_eclipse_route_map_product_tt(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &summary, &diag);
}
result=taiyin_make_call_result(taiyin_call_result_status(result), query_flags|taiyin_call_result_flags(result));
if(taiyin_call_result_status(result)==0) {
 if(count>capacity||count!=capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
eclipse_pack(summary,reply->values);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 36: { // taiyin_compute_solar_eclipse_route_map_product_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const uint64_t low2=web_integer<uint32_t>(in.next());
const uint64_t arg2=low2|(static_cast<uint64_t>(web_integer<uint32_t>(in.next()))<<32);
const size_t arg3=web_integer<size_t>(in.next());
std::vector<taiyin_solar_eclipse_route_product_point> output;
size_t capacity=web_integer<size_t>(in.next()); size_t count=0;
if(capacity>1000000) throw std::invalid_argument("oversized eclipse capacity");
taiyin_solar_eclipse_route_product_summary summary; taiyin_solar_eclipse_route_product_summary_init(&summary);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
if(capacity!=0) throw std::invalid_argument("dynamic capacity must be zero");
result=taiyin_compute_solar_eclipse_route_map_product_ut(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &summary, &diag);
const uint32_t query_flags=taiyin_call_result_flags(result);
if(taiyin_call_result_status(result)!=0) break;
capacity=count; if(capacity>1000000) throw std::invalid_argument("oversized eclipse output");
output.resize(capacity); for(size_t i=0;i<capacity;++i) eclipse_zero_init(&output[i]);
if(capacity!=0) {
result=taiyin_compute_solar_eclipse_route_map_product_ut(context, &arg1, arg2, arg3, output.empty()?0:output.data(), capacity, &count, &summary, &diag);
}
result=taiyin_make_call_result(taiyin_call_result_status(result), query_flags|taiyin_call_result_flags(result));
if(taiyin_call_result_status(result)==0) {
 if(count>capacity||count!=capacity) throw std::runtime_error("invalid eclipse count");
 reply->values[2]=static_cast<double>(count);
eclipse_pack(summary,reply->values);
for(size_t i=0;i<count;++i) eclipse_pack(output[i],reply->values);
}
break; }
case 37: { // taiyin_compute_local_solar_eclipse_boundary_tt
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const double arg2=static_cast<double>(in.next());
const double arg3=static_cast<double>(in.next());
taiyin_local_solar_eclipse_boundary output; taiyin_local_solar_eclipse_boundary_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_local_solar_eclipse_boundary_tt(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
case 38: { // taiyin_compute_local_solar_eclipse_boundary_ut
taiyin_split_julian_date arg1; eclipse_read(arg1,in);
const double arg2=static_cast<double>(in.next());
const double arg3=static_cast<double>(in.next());
taiyin_local_solar_eclipse_boundary output; taiyin_local_solar_eclipse_boundary_init(&output);
if(in.cursor!=in.count) throw std::invalid_argument("extra eclipse input");
result=taiyin_compute_local_solar_eclipse_boundary_ut(context, &arg1, arg2, arg3, &output, &diag);
if(taiyin_call_result_status(result)==0) eclipse_pack(output,reply->values);
break; }
}
diagnostic(diag,reply->values.data());
reply->values[0]=taiyin_call_result_flags(result); reply->values[1]=taiyin_call_result_status(result);
return reply.release();
} catch(const std::bad_alloc&) { return 0; } catch(const std::invalid_argument&) { return web_error_reply(TAIYIN_ERROR_INVALID_ARGUMENT); } catch(...) { return web_error_reply(TAIYIN_ERROR_INTERNAL); }
}
size_t taiyin_web_eclipse_length(const WebEclipseReply* r) { return r->values.size(); }
const double* taiyin_web_eclipse_data(const WebEclipseReply* r) { return r->values.data(); }
void taiyin_web_eclipse_destroy(WebEclipseReply* r) { delete r; }
}
