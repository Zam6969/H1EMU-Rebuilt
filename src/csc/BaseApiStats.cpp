// BaseApi diagnostics: the "Stats for BaseApi" log dump and the metrics
// reporter that publishes the connection's UDP rates.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/String.h"
#include "udp/UdpRefCount.h"

namespace rebuild::csc {
namespace {

template <typename... Args>
void Log(const char* channel, uintptr_t format, Args... args) {
  game::Call<void (*)(const char*, const char*, ...)>(0x1402bab70)(channel, reinterpret_cast<const char*>(format),
                                                                   args...);
}

int64_t I64(const uint8_t* p, size_t offset) { return *reinterpret_cast<const int64_t*>(p + offset); }
int I32(const uint8_t* p, size_t offset) { return *reinterpret_cast<const int*>(p + offset); }

void* GuardedConnectionField(uint8_t* connection, size_t offset) {
  auto* guard = reinterpret_cast<udp::UdpPlatformGuardObject*>(connection + 0x2E0);
  guard->Enter();
  void* value = game::Field<void*>(connection, offset);
  guard->Leave();
  return value;
}

}  // namespace

// 0x1415f5030: DumpStats(logChannel). Logs manager, connection and reliable
// channel statistics; null/empty channel = the manager's own log channel.
void BaseApiDumpStats(uint8_t* api, const char* channel) {
  if (game::Field<void*>(api, 0x2C0)) {
    soeutil::IString destination{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
    game::Call<void (*)(void*, soeutil::IString*)>(0x140346b50)(game::Field<void*>(api, 0x2C0), &destination);
    if (!channel || !*channel) channel = game::Field<const char*>(game::Field<uint8_t*>(api, 0x2D0), 0x250);
    Log(channel, 0x1424b0728, destination.data);  // "Stats for BaseApi: %s"
    Log(channel, 0x1424b0740);                    // "\tManager:"
    if (GuardedConnectionField(game::Field<uint8_t*>(api, 0x2C0), 0xE0)) {
      void* manager = GuardedConnectionField(game::Field<uint8_t*>(api, 0x2C0), 0xE0);
      alignas(8) uint8_t stats[0xD0];
      game::Call<void (*)(void*, uint8_t*)>(0x140341130)(manager, stats);  // UdpManager::GetStats
      Log(channel, 0x1424b0750, I64(stats, 0x80));  // iterations
      Log(channel, 0x1424b0768, I64(stats, 0x58));  // resentPacketsTimedOut
      Log(channel, 0x1424b0788, I64(stats, 0x90));  // socketOverflowErrors
      Log(channel, 0x1424b07a8, I64(stats, 0x98));  // maxPollingTimeExceeded
      Log(channel, 0x1424b07c8, I32(stats, 0xC4));  // poolAvailable
      Log(channel, 0x1424b07e0, I32(stats, 0xC0));  // poolCreated
    }
    alignas(8) uint8_t connectionStats[0x90];
    game::Call<void (*)(void*, uint8_t*)>(0x140346cf0)(game::Field<void*>(api, 0x2C0), connectionStats);
    Log(channel, 0x1424b07f8);                              // "\tConnection:"
    Log(channel, 0x1424b0750, I64(connectionStats, 0x68));  // iterations
    Log(channel, 0x1424b0808, I64(connectionStats, 0x58));  // applicationPacketsSent
    Log(channel, 0x1424b0828, I32(connectionStats, 0x80));  // averagePingTime
    alignas(8) uint8_t status[0x68];
    game::Call<void (*)(void*, int, uint8_t*)>(0x140346a60)(game::Field<void*>(api, 0x2C0), 4, status);
    Log(channel, 0x1424b0840);                      // "\tReliable Channel:"
    Log(channel, 0x1424b0858, I32(status, 0x00));   // totalPendingBytes
    Log(channel, 0x1424b0870, I32(status, 0x04));   // queuedPackets
    Log(channel, 0x1424b0888, I32(status, 0x08));   // queuedBytes
    Log(channel, 0x1424b08a0, I32(status, 0x14));   // oldestUnacknowledgedAge
    Log(channel, 0x1424b08c0, I32(status, 0x30));   // congestionWindowSize
    Log(channel, 0x1424b08e0, I32(status, 0x38));   // oldestUnacknowledgedLastSend
    Log(channel, 0x1424b0908, I32(status, 0x3C));   // oldestUnacknowledgedAttemptCount
    Log(channel, 0x1424b0930, I64(status, 0x58));   // reliableOutgoingId
    Log(channel, 0x1424b0950, I64(status, 0x60));   // reliableOutgoingPendingId
    soeutil::StringRelease(&destination);
  }
  int64_t now;
  game::Field<int64_t>(api, 0x2C8) = *game::Call<int64_t* (*)(int64_t*)>(0x14032fe90)(&now);
}

// 0x1415f58b0: publish "<prefix>/Udp/..." metrics through registry slot 2
// AddMetric(name, value, description, 0).
void BaseApiReportMetrics(uint8_t* api, void* registry, const char* prefix) {
  auto* manager = game::Field<uint8_t*>(api, 0x2D0);
  if (!manager || !game::Field<void*>(manager, 0x240)) return;
  soeutil::StringFixed<256> name;
  name.data = soeutil::EmptyStringData();
  name.length = 0;
  name.capacity = 0;
  name.vtable = reinterpret_cast<void**>(0x142049e08);  // StringFixed<256>
  alignas(8) uint8_t stats[0xD0];
  game::Call<void (*)(void*, uint8_t*)>(0x140341130)(game::Field<void*>(manager, 0x240), stats);
  using AddMetricFn = void (*)(void*, const char*, int64_t, const char*, int);
  auto addMetric = [&](uintptr_t nameFormat, int64_t value, uintptr_t description) {
    reinterpret_cast<AddMetricFn>((*static_cast<void***>(registry))[2])(registry, name.data, value,
                                                                       reinterpret_cast<const char*>(description), 0);
    (void)nameFormat;
  };
  auto rate = [&](size_t statsOffset, size_t slot, bool perSecond) {
    void* counter = api + statsOffset;
    auto** vtable = *static_cast<void***>(counter);
    if (perSecond) return reinterpret_cast<int (*)(void*, int)>(vtable[slot])(counter, 1000);
    return reinterpret_cast<int (*)(void*)>(vtable[slot])(counter);
  };
  auto format = [&](uintptr_t nameFormat) {
    soeutil::StringFormat(&name, reinterpret_cast<const char*>(nameFormat), prefix);
  };
  format(0x1424b0978);  // "%s/Udp/IncomingByteRate"
  addMetric(0, rate(0x368, 4, true), 0x1424b0990);
  format(0x1424b09b0);  // "%s/Udp/IncomingPacketRate"
  addMetric(0, rate(0x368, 5, true), 0x1424b09d0);
  format(0x1424b09f0);  // "%s/Udp/IncomingPacketSize"
  addMetric(0, rate(0x368, 6, false), 0x1424b0a10);
  format(0x1424b0a38);  // "%s/Udp/OutgoingByteRate"
  addMetric(0, rate(0x780, 4, true), 0x1424b0a50);
  format(0x1424b0a70);  // "%s/Udp/OutgoingPacketRate"
  addMetric(0, rate(0x780, 5, true), 0x1424b0a90);
  format(0x1424b0ab0);  // "%s/Udp/OutgoingPacketSize"
  addMetric(0, rate(0x780, 6, false), 0x1424b0ad0);
  format(0x1424b0af8);  // "%s/Udp/OutgoingPendingKB"
  unsigned pending = game::Call<unsigned (*)(void*)>(0x140344e50)(
      game::Field<void*>(game::Field<uint8_t*>(api, 0x2D0), 0x240));  // TotalPendingBytes (all connections)
  addMetric(0, static_cast<int64_t>(pending >> 10), 0x1424b0b20);
  auto* udpManager = game::Field<uint8_t*>(game::Field<uint8_t*>(api, 0x2D0), 0x240);
  if (game::Field<bool>(udpManager, 0x190)) {  // event queuing enabled
    format(0x1424b0b70);  // "%s/Udp/EventQueueCount"
    addMetric(0, I32(stats, 0xB0), 0x1424b0b88);
    format(0x1424b0bc0);  // "%s/Udp/EventQueueBytes"
    addMetric(0, I32(stats, 0xB4), 0x1424b0bd8);
  }
  name.vtable = reinterpret_cast<void**>(0x142049de8);  // IStringFixed<char,256>
  soeutil::StringRelease(&name);
}

REBUILD_FUNCTION(BaseApi_DumpStats, 0x1415f5030, BaseApiDumpStats);
REBUILD_FUNCTION(BaseApi_ReportMetrics, 0x1415f58b0, BaseApiReportMetrics);

}  // namespace rebuild::csc
