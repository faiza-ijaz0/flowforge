#include "time_format.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace flowforge::server {

std::string to_iso8601(infra::TimePoint time_point) {
  using namespace std::chrono;
  const auto ms_since_epoch = duration_cast<milliseconds>(time_point.time_since_epoch());
  const std::time_t seconds = duration_cast<::std::chrono::seconds>(ms_since_epoch).count();
  const long long milliseconds_part = ms_since_epoch.count() % 1000;

  std::tm utc_tm{};
#if defined(_WIN32)
  gmtime_s(&utc_tm, &seconds);
#else
  gmtime_r(&seconds, &utc_tm);
#endif

  std::array<char, 32> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ", utc_tm.tm_year + 1900,
                utc_tm.tm_mon + 1, utc_tm.tm_mday, utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec,
                milliseconds_part);
  return {buffer.data()};
}

}  // namespace flowforge::server
