# H1Z1 client rebuild

A function-by-function C++ reimplementation of `H1Z1.exe` (the 2016-12-20 build,
timestamp `0x5859C0E4`). The original exe keeps running; each rebuilt function
replaces its original through a hook, so the game stays playable at every step
and any single rebuild can be switched off to find a regression.

## How it loads

`H1Z1.exe` imports `VERSION.dll`. This project builds a `version.dll` that sits
in the game folder, forwards all 17 real exports to `version_orig.dll` (a copy
of the system DLL), and installs the hooks from `DllMain`, before any game code
runs. The existing `dinput8.dll` mod loader in the game folder is left alone.

## Build and install

```
powershell -ExecutionPolicy Bypass -File tools\build.ps1
powershell -ExecutionPolicy Bypass -File tools\install.ps1            # into C:\Users\zam\Documents\H1emu
powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Uninstall
```

Requires VS Build Tools with the C++ workload (MSVC + bundled CMake).
`build.ps1` finishes by running `hookcheck.exe`, which maps the real
`H1Z1.exe` as an image (without running it) and installs every hook against
it, so a wrong address or an unpatchable function fails the build.

`rebuild.ini` in the game folder: `enabled=0` runs the stock client,
`console=1` opens a log window, `[hooks] Name=0` disables one rebuild.
The log is written to `rebuild.log` next to the exe.

## Adding a rebuilt function

1. Decompile in Ghidra (project `C:\Users\zam\h1.gpr`) and rename the function
   there, e.g. `UdpPlatformDriver_SocketSend`, so the Ghidra project and this
   code stay in sync.
2. Write it in C++ under `src/<system>/`, matching the original signature exactly
   (`this` becomes the first parameter). Reconstruct struct layouts with
   `static_assert(offsetof(...))` against the offsets seen in the disassembly.
3. Call code that isn't rebuilt yet with `game::Call<Fn>(address)`. Allocate and
   free through `soeutil::Allocate/Free` (the game's operator new/delete).
4. Register it: `REBUILD_FUNCTION(Name, 0x14xxxxxxx, Function);` For originals
   under 5 bytes use `REBUILD_FUNCTION_TOO_SMALL` (kept as source, not hooked).
5. Build (runs hookcheck), install, play, and check `rebuild.log`.

Check return types in the disassembly: MSVC returns `bool` in `AL` only, so
a function ending in `mov al, 1` must be rebuilt as `bool`, not `int`.
Identical function bodies are folded by the linker (e.g. `return this->int_08`
is one function shared by many classes); a rebuild of such a body must stay
correct for every class that points at it.

`tools/rtti_names.py` names every virtual function in Ghidra after its owning
class from the RTTI in the exe (`Namespace__Class_vfNN`). Only 571 vtables
carry RTTI; most engine classes were built without it.

## Progress

| System | Class | Rebuilt |
|---|---|---|
| SoeUtil | allocator | complete: TLS slot, SetThreadAllocator, MemoryAllocate/Free, default aligned alloc/free |
| SoeUtil | `HashListMap<int,uint64,1024,-1>` | layout, Increment (inlined at 2 sites), Clear |
| UdpLibrary | `UdpPlatformDriver` | complete: ctor, dtor, all 16 vtable slots, error counters (20 functions) |
| UdpLibrary | `UdpPlatformGuardObject` | complete: ctor, dtor, Enter, Leave |
| UdpLibrary | `UdpRefCount`, `UdpGuardedRefCount` | complete: all 5 vtable slots each |
| UdpLibrary | `UdpPlatformThreadObject` | complete: ctor, dtor, Start, IsRunning, thread proc |
| UdpLibrary | `UdpManagerThread` | complete: ctor, dtor, Run |
| UdpLibrary | `LogicalPacket` family | complete: Logical, Simple, Group (incl. AddPacket), Pooled, Fixed<128/256/512/1024> |
| UdpLibrary | `UdpMisc` | Put/GetVariableValue, Random, NextPrime, CreateQuickLogicalPacket |
| UdpLibrary | `UdpManager::Params` | constructor with all 5 role presets |
| SoeUtil | `Mutex` | Construct (named, spin count), Destroy, Lock, Unlock (critical section or Win32 mutex mode) |
| SoeUtil | `Time` | TimeNow (QueryPerformanceCounter ms) |
| SoeUtil | `ByteStream` | layout (Array<uchar,8192,1> + cap + cursor, 0x2038) and the inlined stack/pooled stream setup, Put, teardown |
| SoeUtil | `Array` / read cursor | Array::Resize (grow, size = min(count, capacity)), ReadBytes |
| SoeUtil | `IString` / `StringFixed<N>` | layout (copy-on-write shared buffer with share count at data-4, inline N+4 buffer), Reserve, Assign, AssignString (buffer sharing), AssignN, IString Allocate/Free, and Allocate/Free/destructors for all 20 StringFixed sizes in the exe (5 to 65536) |
| ClientServerCore | `UdpCompressionHandler` | Encrypt/Decrypt = packet compression (flag byte 01 deflate level 6 / 00 stored); dtor, GetStats, ClearStats |
| ClientServerCore | `BaseApi` incoming path | OnRoutePacket (stats, RPC routing, game dispatch via vtable slot 18, slow-packet logging), RPC router (64-bucket id hash, BE/LE ids), SoeUtil hex dump |
| ClientServerCore | `BaseApi` connection API | Connect(addresses, timeout, autoReconnect) with SoeUtil::Random address shuffle, internal Connect (address rotation, EstablishConnection), OnTerminated (reason, full stats log, OnDisconnect/OnFailed, release), IsConnected / IsConnecting, Send (raw + LogicalPacket), OnConnectComplete, Disconnect, Reconnect, GiveTime (auto-reconnect), WaitForConnect / WaitForDisconnect / WaitForFlush, guarded UdpConnection disconnect |
| ClientServerCore | `BaseUdpManager` | constructor (params role, ports, timeouts, compression / threading, large-buffer profile), SetInifileSection, SetLogChannel (+ BaseApi forwarder), ServiceStart (ini settings, UdpManager creation, socket errors, thread start), ServiceStop, GiveTime (event queuing, 30 s ini reload of UdpParams); UdpManager::SetHandler |
| ClientServerCore | RPC plumbing | RpcRouter ctor/dtor/RemoveHandler, handler table Clear/Remove, ByteStream::Put, RpcHandlerTable ctor, 11 u16 id serializers (LE + big-endian template instances), BasePacket dtor, ByteStream dtor |
| ClientServerCore | lifetime + accessors | BaseApi destructor (flush, release, owned-manager delete), UdpCompressionHandler ctor, BaseApi constructors (shared or own manager: RpcRouter, rate trackers, strings, address list), BaseApi field init, metrics group registration, address-list splitter (SoeUtil List<IString>), Array<IString>::RemoveRange, ~BaseUdpManager, ~RpcManagerHandler, ~UdpConnectionHandler, BaseApi SetServer, reliable-channel stats (resend ratios), cached connection counters |
| ClientServerCore | `BaseApi` diagnostics | DumpStats (manager / connection / reliable channel log dump), ReportMetrics (Udp rate / size / pending / event-queue metrics) |
| ClientServerCrypto | `CryptoBaseApi` | both constructors (shared / own manager), cipher factory (types 1-3), ArraySecure key storage ctor / assign / HasKey, destructor (session + secure ciphers), SetSessionKey (cipher factory + init), EncodeSecure; CryptoKey ctor / IsValid |
| Crypto | `CipherRC4` (type 3) + `Cipher` base | complete: RC4 key schedule + keystream, ctor, dtor, Init (per-direction states), Encode / Decode, size / type / ready queries, Rekey; Cipher base dtor |
| Crypto | `CipherAES` (type 1) | complete class: ctor / dtor (key wipe), Init (enc + dec key schedules), padded ECB Encode / Decode, block counters + NeedsRekey limits, size / type / ready queries (AES block + key-schedule primitives still called by address) |
| Crypto | `CipherCCM` (type 2) | ctor / dtor, 13-byte nonce builder + Array<uchar,13> dtor, block counter, Init (PRNG seed + CCM key), size / type / ready / key-size queries, NeedsRekey (Encode / Decode with nonce + replay check still original) |
| UdpLibrary | `UdpManager` | constructor (params fix-up, driver, receive ring, packet pool prefill, memory pools, hash tables, priority queue, port-range bind), ClearStatistics, destructor (incl. memory-pool teardown), OpenSocket, EstablishConnection, ExpectIncoming (NAT punch-through, 00 1F probes), DisconnectAll, PopEvent, DumpPacketHistory, list teardown helpers (5), GiveTime, ProcessRawPacket (incoming dispatch: connect, remap, unknown-terminate, unreachable reply), ActualReceive/Send/SendHelper, NextIncomingPacket, CreatePacket, connection lookup (by address and code), AddNewConnection, address/code hash tables (resize, insert, remove), event queue (alloc, clear, release, queue, DeliverEvents), all 6 handler callbacks, packet pool (3), clock (2), bandwidth buckets (2), priority-queue reprioritize, disconnect cleanup (4), SimulateQueueEntry ctor |
| UdpLibrary | `UdpConnection` tick | both constructors (incoming / outgoing), destructor, deleting destructor, InternalGiveTime (connect retries, clock sync, reliable channels, multi-buffer hold, keep-alive, port-alive, disconnect drain, no-data timeout), ProcessApplicationPacket, Age, TotalPendingBytes, SetOtherSideTerminated; sync stamps + SyncStampShortDelta; manager SendPortAlive |
| UdpLibrary | `UdpConnection` API | Send (all 8 channels: unreliable, unbuffered, ordered 1A/1B, reliable 1-4, with promotion to reliable), FlushNow, DrainSendQueue, GetChannelStatus, GetStats, GetDestinationIp/String, GetDisconnectReasonText, DisconnectReasonText (official reason names) |
| UdpLibrary | `UdpConnection` | Init, SetupEncryption, GiveTime, GetStatus, Disconnect, Clock, ProcessRawPacket (CRC verify + decrypt), corrupt/ICMP handling, receive buckets, Elapsed, PhysicalSend (2-pass encrypt + CRC), PhysicalSendFinal, send buckets, SendTerminatePacket, FlushChannels, all 5 handler callbacks, all 10 encrypt/decrypt methods; manager-side Schedule/AddDisconnecting/RemoveConnection, priority-queue Update |
| UdpLibrary | `UdpConnection` dispatch | ProcessCookedPacket: every protocol opcode (connect/confirm, multi, terminate, clock sync/reflect, reliable/ack routing, group, ordered, unreachable/remap); BufferedSend (00 03 multi-packet batching) |
| UdpLibrary | `UdpReliableChannel` | complete: ctor/dtor + ring element ctors/dtors, ReliablePacket (window, reordering, ack-dedup), Ack/AckAll/AckInternal (congestion window, RTT), fragment reassembly, Send / SendCoalesce (00 19 groups) / FlushCoalesce / QueueLogicalPacket / PullDataFromQueue (fragmentation), GiveTime (resend + congestion control: RFC 3390 initial window, loss and timeout back-off), GetChannelStatus, ClearStats; UdpMisc::Clock |
| UdpLibrary | `UdpMisc` CRC | Crc32 (CRC-32 primed with the encrypt code) |
| UdpLibrary | guarded stats | UdpConnection Outgoing/IncomingBytesLastSecond, LastReceive, LastSend; UdpManager LastEventAge, GetStats |
| Game | packet dispatch | Login::ExternalLoginUdpApi::HandlePacket (10 LoginUdp_11 replies), Gateway::ExternalGatewayApi::HandlePacket (opcode & 0x1F / channel >> 5: LoginReply, ForceDisconnect, TunnelPacket, ChannelIsRoutable, ConnectionIsNotRoutable) |
| Game | `Gateway::ExternalGatewayApi` | both constructors (shared / own manager), destructor, Init (character id, ticket, protocol, build, log prefix), Connect override, Send override (queue reliable / drop unreliable before login), SendPacketSecure (encode-before-queue), SendTunnelPacket (opcode 6 | channel), ForcedLogout::Read, u64 serializer, deleting dtor, header-only packet unserializer + 3 serializers, 6 packet destructors, SendLoginRequest (+ PacketLoginRequest serializer / destructor, IString::Write), SendPacket (header-only packets, queued in the DataQueue until login), Logout, OnConnect / OnDisconnect / OnFailed, OnLoginReply (flushes queued packets), LoginReply / ForcedLogout / ChannelIsRoutable unserializers, OnChannelIsRoutable, OnConnectionIsNotRoutable, OnForcedLogout, OnTunnelPacket (listener at +0x1058) |
| Game | `Login::ExternalLoginUdpApi` handlers | all 10 reply handlers: LoginReply, ForceDisconnect, CharacterCreate/Login/Delete/SelectInfo/TransferReply, ServerListReply, ServerUpdate, TunnelAppPacketServerToClient (listener at +0x10) |
| Game | `Login::ExternalLoginUdpApi` requests | Logout (+ transport disconnect), ServerListRequest (+ their UDP / XML senders), EntityDetails list + ErrorDetail array destructors, ErrorDetail array Resize/Clear/RemoveLast, entity list Clear, CharacterCreate / Login / Delete / SelectInfo request senders (UDP or alternate transport), their UDP send + serializers, Array<uchar>::Write, and the XML (BaseTcpApi) senders |
| Game | `Login::ExternalLoginUdpApi` unserializers | all 10: ServerUpdate, LoginReply, ForcedDisconnect, CharacterCreate/Login/Delete/SelectInfo/TransferReply, ServerListReply, TunnelAppPacketServerToClient (packet layouts static_assert'd) + their Read functions: LoginReply, CharacterLogin/Delete/Transfer/SelectInfo, TunnelApp, server list, and the member readers: IString::Read, byte arrays, EntityDetails, ClientGameServerData, account-feature HashListMap, error details |
| Game | zone connection wrapper | constructor (gateway api with own manager, list, mutex), destructor, FlushQueue (sends queued packets in order, reports each to the packet recorder), SetSessionKey, queued-packet list (destroy, clear, deleting dtor) node alloc/free, and the 3 queued-packet classes (ctor, deleting dtors, Send via the gateway api: plain/reliable/secure or tunnel, kind/channel/stream accessors), 14 accessors over the gateway api: stats, counters, character id, UdpConnection / UdpManager / params, IsConnecting / IsConnected / IsLoggedIn, session key, SetLoginPending, SetListener, Disconnect |
| Game | zone client (gateway listener) | OnConnect, OnChannelIsRoutable, OnConnectionIsNotRoutable, OnTunnelData (zone packet entry: channel 2 = opcode 0x79 packets, opcode 0xF5 float, rest -> game packet handler), 4 no-op slots |
| Game | game client zone packet entry | HandleZonePacket (slot 42 of the game client, vtable 0x1420646e0): sequence + timestamp, dispatch now vs. time-ordered delay queue, per-channel last-packet record; slot 73 HandleChannel2Packet (channel-2 packets -> slot 72) |
| Game | zone opcode dispatcher | DispatchZonePacket 0x1403fe210, fully rebuilt: all 168 built-in opcodes (SendSelfToClient 0x03, single-call routes to subsystems, ZoneDoneSendingInitialData 0x05, KickedFromServer 0x2F, stack-packet readers 0x25/0x3D/0x69/0xAA/0xAB/0xAF/0xB6/0xE6/0xEE, constructed packets 0x25/0x33/0x97/0xA8/0xAE/0xB0/0xB1/0xCB (0xB0 falls into 0xB1 on a failed read, as in the original), click-to-move reply 0x3E, 0xDE (its 0x200F8-byte object moved from the stack to the heap), begin zoning 0x0B, the 0xE3 sub-dispatcher incl. the matchresults CSV writer, reader-pattern packets 0x79/0xD6/0xD7/0xD8/0xDB/0xDC, 0xC5, id-list 0xD5, 0x35, localized message 0x43, 0x4F, race/checkpoint stopwatch UI 0x78, 0x44, 0x61, 0x65, loyalty info 0x7D, trial job expired 0x62, membership activated 0x40, timed kick 0x3F, server message 0x08, server shutdown + BadPackets.txt dump 0x2C, session details 0x30, ZoneDetails 0x16 (0x99 falls into it, as in the original), opcode-only 0xB9, login-failed shutdown 0x51, lazy-init routes, 0x32 value packet), plus the extension-handler default for the 78 table gaps and out-of-range opcodes; the original's fallthroughs (0x99 -> 0x16, 0xB0 -> 0xB1) are kept. `REBUILD_FUNCTION_WITH_ORIGINAL` exists for future partial rebuilds |
| GameCommerce | marketing-data containers | two HashListMap instances: Remove, Clear, Rehash (templated over node layout) |

Total: 624 functions (601 hooked, 23 too small to hook).

Next: the rest of `UdpConnection` (internal GiveTime 0x140347360,
ProcessRawPacket 0x1403491e0, the big packet handler 0x140348390,

constructor 0x140345090), then the `UdpManager` constructor/destructor and
the SoeUtil DynamicMemoryPool it owns at +0x610.

Third-party code in the exe (PhysX/APEX, curl, Vivox, Steam API, MSVC CRT/STL)
is not rebuilt by hand: it gets identified and linked from the real libraries.
