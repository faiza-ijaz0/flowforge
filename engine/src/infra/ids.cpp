#include "flowforge/infra/ids.hpp"

#include <algorithm>
#include <array>
#include <random>

namespace flowforge::infra {

namespace {

std::mt19937_64& thread_local_rng() {
  thread_local std::mt19937_64 rng{std::random_device{}()};
  return rng;
}

}  // namespace

std::string generate_uuid_v4() {
  std::uniform_int_distribution<int> nibble_dist(0, 15);
  auto& rng = thread_local_rng();

  static constexpr std::array<char, 16> kHex = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

  std::string uuid(36, '-');
  static constexpr std::array<int, 4> kDashPositions = {8, 13, 18, 23};
  for (int i = 0; i < 36; ++i) {
    const bool is_dash = std::ranges::find(kDashPositions, i) != kDashPositions.end();
    if (is_dash) {
      continue;
    }
    int nibble;
    if (i == 14) {
      nibble = 4;  // version 4
    } else if (i == 19) {
      nibble = 8 + (nibble_dist(rng) % 4);  // variant 1 (10xx)
    } else {
      nibble = nibble_dist(rng);
    }
    uuid[static_cast<std::size_t>(i)] = kHex[static_cast<std::size_t>(nibble)];
  }
  return uuid;
}

bool is_uuid(std::string_view value) noexcept {
  if (value.size() != 36) {
    return false;
  }
  for (std::size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') {
        return false;
      }
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

}  // namespace flowforge::infra
