#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// Two-page, copy-on-write snapshots. The commit word is programmed last.
// A torn write never supersedes the previous complete snapshot.
namespace Journal {
constexpr uint32_t first = 0xE5000, pageSize = 4096, magic = 0x4D314E56;
inline uint32_t crc(const void *data, size_t size) {
  uint32_t c = ~0u;
  const auto *p = static_cast<const uint8_t *>(data);
  while (size--) {
    c ^= *p++;
    for (unsigned b = 0; b < 8; ++b)
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}
struct Header {
  uint32_t sequence, size, checksum, committed;
};
template <class Data, class Flash> class Store {
  static_assert(sizeof(Data) + sizeof(Header) <= pageSize, "Snapshot too large");
  static_assert(sizeof(Data) % 4 == 0, "Flash data must be word aligned");
  Flash &flash_;
  unsigned active_ = 1;
  uint32_t sequence_ = 0;

public:
  explicit Store(Flash &f) : flash_(f) {}
  bool load(Data &data) {
    bool found = false;
    for (unsigned slot = 0; slot < 2; ++slot) {
      Header h{};
      Data candidate{};
      const uint32_t addr = first + slot * pageSize;
      if (!flash_.read(addr, &h, sizeof(h)) || h.committed != magic || h.size != sizeof(Data) ||
          !flash_.read(addr + sizeof(h), &candidate, sizeof(candidate)) ||
          h.checksum != crc(&candidate, sizeof(candidate)))
        continue;
      if (!found || int32_t(h.sequence - sequence_) > 0) {
        data = candidate;
        active_ = slot;
        sequence_ = h.sequence;
        found = true;
      }
    }
    return found;
  }
  bool save(const Data &data) {
    const unsigned next = active_ ^ 1;
    const uint32_t addr = first + next * pageSize;
    Header h{sequence_ + 1, sizeof(Data), crc(&data, sizeof(data)), 0xFFFFFFFF};
    if (!flash_.erase(addr) || !flash_.write(addr, &h, sizeof(h) - 4) ||
        !flash_.write(addr + sizeof(h), &data, sizeof(data)))
      return false;
    const uint32_t commit = magic;
    if (!flash_.write(addr + offsetof(Header, committed), &commit, sizeof(commit)))
      return false;
    Header check{};
    Data saved{};
    if (!flash_.read(addr, &check, sizeof(check)) || check.sequence != h.sequence ||
        check.size != h.size || check.checksum != h.checksum)
      return false;
    if (check.committed != magic || !flash_.read(addr + sizeof(h), &saved, sizeof(saved)) ||
        std::memcmp(&data, &saved, sizeof(data)))
      return false;
    sequence_ = h.sequence;
    active_ = next;
    return true;
  }
};
} // namespace Journal
