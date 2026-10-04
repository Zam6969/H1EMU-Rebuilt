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
| ClientServerCore | `BaseApi` incoming path | OnRoutePacket (stats, RPC routing, game dispatch via vtable slot 18, slow-packet logging), RpcSendHeaderOnly ({u16 opcode, u16 0} in the endpoint's byte order), RPC router (64-bucket id hash, BE/LE ids), SoeUtil hex dump |
| ClientServerCore | `BaseApi` connection API | Connect(addresses, timeout, autoReconnect) with SoeUtil::Random address shuffle, internal Connect (address rotation, EstablishConnection), OnTerminated (reason, full stats log, OnDisconnect/OnFailed, release), IsConnected / IsConnecting, Send (raw + LogicalPacket), OnConnectComplete, Disconnect, Reconnect, GiveTime (auto-reconnect), WaitForConnect / WaitForDisconnect / WaitForFlush, guarded UdpConnection disconnect |
| ClientServerCore | `BaseUdpManager` | constructor (params role, ports, timeouts, compression / threading, large-buffer profile), SetInifileSection, SetLogChannel (+ BaseApi forwarder), ServiceStart (ini settings, UdpManager creation, socket errors, thread start), ServiceStop, GiveTime (event queuing, 30 s ini reload of UdpParams); UdpManager::SetHandler |
| ClientServerCore | RPC plumbing | RpcRouter ctor/dtor/RemoveHandler, handler table Clear/Remove, ByteStream::Put, RpcHandlerTable ctor, 11 u16 id serializers (LE + big-endian template instances), BasePacket dtor, ByteStream dtor |
| ClientServerCore | lifetime + accessors | BaseApi destructor (flush, release, owned-manager delete), UdpCompressionHandler ctor, BaseApi constructors (shared or own manager: RpcRouter, rate trackers, strings, address list), BaseApi field init, metrics group registration, address-list splitter (SoeUtil List<IString>), Array<IString>::RemoveRange, ~BaseUdpManager, ~RpcManagerHandler, ~UdpConnectionHandler, BaseApi SetServer, reliable-channel stats (resend ratios), cached connection counters |
| ClientServerCore | `BaseApi` diagnostics | DumpStats (manager / connection / reliable channel log dump), ReportMetrics (Udp rate / size / pending / event-queue metrics) |
| ClientServerCrypto | `CryptoBaseApi` | both constructors (shared / own manager), cipher factory (types 1-3), PacketUtils::WrapPacket (encrypt + 17-byte {type, magic} footer) / UnwrapPacket, Send override (re-wrap with the second cipher or defer to slot 21), SendSecure (slot 21: session-cipher encode, reliable send), SendSecureHeavy (slot 22: wrap with the second cipher, or session-encode when active), OnRoutePacket override (session decode + unwrap, hex-dumps undecodable packets), ArraySecure key storage ctor / assign / HasKey, destructor (session + secure ciphers), SetSessionKey (cipher factory + init), EncodeSecure; CryptoKey ctor / IsValid |
| Crypto | `CipherRC4` (type 3) + `Cipher` base | complete: RC4 key schedule + keystream, ctor, dtor, Init (per-direction states), Encode / Decode, size / type / ready queries, Rekey; Cipher base dtor |
| Crypto | `CipherAES` (type 1) | complete class: ctor / dtor (key wipe), Init (enc + dec key schedules), padded ECB Encode / Decode, block counters + NeedsRekey limits, size / type / ready queries (AES block + key-schedule primitives still called by address) |
| Crypto | `CipherCCM` (type 2) | ctor / dtor, 13-byte nonce builder + Array<uchar,13> dtor, block counter, Init (PRNG seed + CCM key), size / type / ready / key-size queries, NeedsRekey, Encode (ciphertext + 16-byte tag + 13-byte nonce, counter++, block count) and Decode (in place; rejects zero nonce/tag, replayed counters, auth failures), PRNG seeding (8 LCG words ^ salt) - CipherCCM is now complete apart from the bundled CCM library |
| UdpLibrary | `UdpManager` | constructor (params fix-up, driver, receive ring, packet pool prefill, memory pools, hash tables, priority queue, port-range bind), ClearStatistics, destructor (incl. memory-pool teardown), OpenSocket, EstablishConnection, ExpectIncoming (NAT punch-through, 00 1F probes), DisconnectAll, PopEvent, DumpPacketHistory, list teardown helpers (5), GiveTime, ProcessRawPacket (incoming dispatch: connect, remap, unknown-terminate, unreachable reply), ActualReceive/Send/SendHelper, NextIncomingPacket, CreatePacket, connection lookup (by address and code), AddNewConnection, address/code hash tables (resize, insert, remove), event queue (alloc, clear, release, queue, DeliverEvents), all 6 handler callbacks, packet pool (3), clock (2), bandwidth buckets (2), priority-queue reprioritize, disconnect cleanup (4), SimulateQueueEntry ctor |
| UdpLibrary | `UdpConnection` tick | both constructors (incoming / outgoing), destructor, deleting destructor, InternalGiveTime (connect retries, clock sync, reliable channels, multi-buffer hold, keep-alive, port-alive, disconnect drain, no-data timeout), ProcessApplicationPacket, Age, TotalPendingBytes, SetOtherSideTerminated; sync stamps + SyncStampShortDelta; manager SendPortAlive |
| UdpLibrary | `UdpConnection` API | Send (all 8 channels: unreliable, unbuffered, ordered 1A/1B, reliable 1-4, with promotion to reliable), FlushNow, DrainSendQueue, GetChannelStatus, GetStats, GetDestinationIp/String, GetDisconnectReasonText, DisconnectReasonText (official reason names) |
| UdpLibrary | `UdpConnection` | Init, SetupEncryption, GiveTime, GetStatus, Disconnect, Clock, ProcessRawPacket (CRC verify + decrypt), corrupt/ICMP handling, receive buckets, Elapsed, PhysicalSend (2-pass encrypt + CRC), PhysicalSendFinal, send buckets, SendTerminatePacket, FlushChannels, all 5 handler callbacks, all 10 encrypt/decrypt methods; manager-side Schedule/AddDisconnecting/RemoveConnection, priority-queue Update |
| UdpLibrary | `UdpConnection` dispatch | ProcessCookedPacket: every protocol opcode (connect/confirm, multi, terminate, clock sync/reflect, reliable/ack routing, group, ordered, unreachable/remap); BufferedSend (00 03 multi-packet batching) |
| UdpLibrary | `UdpReliableChannel` | complete: ctor/dtor + ring element ctors/dtors, ReliablePacket (window, reordering, ack-dedup), Ack/AckAll/AckInternal (congestion window, RTT), fragment reassembly, Send / SendCoalesce (00 19 groups) / FlushCoalesce / QueueLogicalPacket / PullDataFromQueue (fragmentation), GiveTime (resend + congestion control: RFC 3390 initial window, loss and timeout back-off), GetChannelStatus, ClearStats; UdpMisc::Clock |
| UdpLibrary | `UdpMisc` CRC | Crc32 (CRC-32 primed with the encrypt code) |
| ClientServerCore | `BaseApp` | Init: <name>Startup.log, logging service setup / connect, WorkingDir, crash file name, InfiniteLoopMonitor watchdog, CrashReporter settings (product, version, uploader, dump options), Pid / Version / Built banner |
| ClientServerCore | `BaseConfig` | Load(reload): <app>.ini / BaseInifile resolution, change detection by file time, ini layering (command line -> primary -> base), Settings / Soemon / Profiler / CrashReporter / Logging / InfiniteLoopMonitor / Memory values |
| TcpLibrary | `TcpPlatformDriver` | complete (all 15 slots): deleting destructor, Listen (bind host / gethostbyname, listen backlog), CloseListen, GetLocalAddress / GetLocalPort, GetProxy (IE ProxyEnable / ProxyServer registry), Connect (non-blocking, TcpConnectionPlatformDriver + TcpConnection), GetHostAddress (public / private pick), Init, Resolve ("host[:port]"), InitSsl (Secur32 SSPI, Schannel credentials), CleanupSsl, AcceptConnections, ConnectAsync; TcpDriver::ResolveAsync ("TcpDriverAsyncAddress" pool) - complete |
| TcpLibrary | `TcpConnectionPlatformDriver` | complete (all 11 slots + TLS helpers): deleting destructor, SetOwner, GiveTime (async-address connect, zero-timeout select for connect completion, 64 KB recv loop with per-error disconnect reasons, TLS hand-off), local / remote address, local port, Send (plain or TLS), IsSecure, StartSslClientHandshake (InitializeSecurityContextW ClientHello), TerminateSslConnection (SCHANNEL_SHUTDOWN + close_notify), SendEncrypted (EncryptMessage stream buffers, queue the unsent tail), DecryptReceived (DecryptMessage loop: extra bytes, expiry, incomplete records, renegotiation), DoSslClientHandshake (InitializeSecurityContextW loop, extra-data carry-over, bundled app data) |
| Engine | deleting destructors | 43 functions (src/game/MiscDestructors.cpp): LoggingApi this-adjusting thunks, RefCounted / RefCountedImplicit / ThreadBase (shared count block), CoreGameClient notify slots, AssetDelivery Loader SetName, LoggingApi, log / crash-reporter / profiler / job handlers, Thread, InputThread, CoreGameClient, AssetDelivery Loader + handler, Wwise IO hooks, CryptographicHash, GameCommerce (marketing data, purchase order), Resource decompression job, input manager, task management, TinyHttp request, pugixml, SpeedTree, DataManagement interfaces |
| Networking | deleting destructors | 66 vtable slot-0 / default-handler functions (src/udp/NetDestructors.cpp): UdpLinkedList x6, UdpDriver, Udp manager/connection/compression/RPC handlers, BaseApp, BaseConfig, BaseApi, BaseUdpManager, CryptoBaseApi, BaseTcpApi, GameServerData, ExternalLoginApi, ExternalLoginUdpApi, ExternalLoginTcpApi, BaseApp flag clear, all Gateway and Login external packets (incl. byte-array packets and ServerListReply), secondary-vtable this-adjusting thunks, UdpManagerHandler copy-through encrypt/decrypt defaults |
| UdpLibrary | guarded stats | UdpConnection Outgoing/IncomingBytesLastSecond, LastReceive, LastSend; UdpManager LastEventAge, GetStats |
| Game | packet dispatch | Login::ExternalLoginUdpApi::HandlePacket (10 LoginUdp_11 replies), Gateway::ExternalGatewayApi::HandlePacket (opcode & 0x1F / channel >> 5: LoginReply, ForceDisconnect, TunnelPacket, ChannelIsRoutable, ConnectionIsNotRoutable) |
| Game | `Gateway::ExternalGatewayApi` | both constructors (shared / own manager), destructor, Init (character id, ticket, protocol, build, log prefix), Connect override, Send override (queue reliable / drop unreliable before login), SendPacketSecure (encode-before-queue), SendTunnelPacket (opcode 6 | channel), ForcedLogout::Read, u64 serializer, deleting dtor, header-only packet unserializer + 3 serializers, 6 packet destructors, SendLoginRequest (+ PacketLoginRequest serializer / destructor, IString::Write), SendPacket (header-only packets, queued in the DataQueue until login), Logout, OnConnect / OnDisconnect / OnFailed, OnLoginReply (flushes queued packets), LoginReply / ForcedLogout / ChannelIsRoutable unserializers, OnChannelIsRoutable, OnConnectionIsNotRoutable, OnForcedLogout, OnTunnelPacket (listener at +0x1058) |
| Game | `Login::ExternalLoginUdpApi` handlers | all 10 reply handlers: LoginReply, ForceDisconnect, CharacterCreate/Login/Delete/SelectInfo/TransferReply, ServerListReply, ServerUpdate, TunnelAppPacketServerToClient (listener at +0x10) |
| Game | `Login::ExternalLoginUdpApi` requests | Logout (+ transport disconnect), ServerListRequest (+ their UDP / XML senders), EntityDetails list + ErrorDetail array destructors, ErrorDetail array Resize/Clear/RemoveLast, entity list Clear, CharacterCreate / Login / Delete / SelectInfo request senders (UDP or alternate transport), their UDP send + serializers, Array<uchar>::Write, and the XML (BaseTcpApi) senders |
| Game | `Login::ExternalLoginUdpApi` unserializers | all 10: ServerUpdate, LoginReply, ForcedDisconnect, CharacterCreate/Login/Delete/SelectInfo/TransferReply, ServerListReply, TunnelAppPacketServerToClient (packet layouts static_assert'd) + their Read functions: LoginReply, CharacterLogin/Delete/Transfer/SelectInfo, TunnelApp, server list, and the member readers: IString::Read, byte arrays, EntityDetails, ClientGameServerData, account-feature HashListMap, error details |
| Game | zone connection wrapper | constructor (gateway api with own manager, list, mutex), destructor, Send / SendReliable / SendTunnel (direct on the owning thread after flushing, otherwise copied and queued), SendTunnelMessage (serializes header, byte, int and chunk list), Enqueue, FlushQueue (sends queued packets in order, reports each to the packet recorder), SetSessionKey, queued-packet list (destroy, clear, push-front, deleting dtor) node alloc/free, and the 3 queued-packet classes (ctor, deleting dtors, Send via the gateway api: plain/reliable/secure or tunnel, kind/channel/stream accessors), 14 accessors over the gateway api: stats, counters, character id, UdpConnection / UdpManager / params, IsConnecting / IsConnected / IsLoggedIn, session key, SetLoginPending, SetListener, Disconnect |
| Game | zone client (gateway listener) | constructor (creates the gateway connection with protocol "ClientProtocol_1080", registers as listener, UDP params), destructor + deleting destructor (+ record, array, list and byte-array-list helpers with their deleting dtors, node alloc/free slots, array reserve), listener base deleting dtor and no-op slots, OnConnect, OnChannelIsRoutable, OnConnectionIsNotRoutable, OnTunnelData (zone packet entry: channel 2 = opcode 0x79 packets, opcode 0xF5 float, rest -> game packet handler), 4 no-op slots |
| Game | game client zone packet entry | HandleZonePacket (slot 42 of the game client, vtable 0x1420646e0): sequence + timestamp, dispatch now vs. time-ordered delay queue, per-channel last-packet record; slot 73 HandleChannel2Packet (channel-2 packets -> slot 72); OnZoneConnected 0x140430490; simple slots 5-8, 14-16, 22-23 (opcode-0x61 floats), 53 and the shared pure-virtual crash stub; deleting dtor (slot 0), close-button shutdown (slot 3), slots 26, 54, 62 (UI "OnUpdate" tick), 122-124 (timer pair), 25 (UI hook object), 33, 47, 51, 61 ("GuiOnShutdown"), 79, 84, 119, Shutdown (slot 27, "Unknown error" fallback), CreateZoneClient (slot 86), SendMountRequest (slot 67, #ClientMountLog.txt), asset-handler poll (slot 121), SetLocale (slot 32, rebuilds the string table), slots 13, 71, 72 (entity lookup or request 0xE1), 87, SetLoginInfo (slot 28: character id, ticket, session key), slots 45/46 (console input echo + run), CheckIdle (slot 114: two idle limits from settings), OnInit (slot 60: EVENT_CLIENT_INIT, GuiOnInit or "Main"), HandleEvent (slot 98: respawn-window hide etc.), packet 0x93 handler (slot 92), ShutdownUi (slot 34: closes UI modules, destroys the extension root), HandleInput (slot 10: cursor mode, last-input time, input handler chain), slot 74 (owned-entity update), packet 0x6F (slot 102: server name, "Resources/Images", object at +0x3B948), packet 0x42 sub-type 2 (slot 100: five-section blob), packet 0xE2 sub-types 1/2 (slot 94), Log (slot 91: '#'/"Tcp" channels also go to the server as packet 0x48), LoadOptions (slot 81: Country, loading screens, LiveGamer/Steam/UseNewUI/splash from the ini), StartLogging (slot 80: H1Z1.log in ./Logs, version/command line/launcher lines), WaitForCharacterLogin (slot 39: pump the gateway up to 120 s for our character, EVENT_LOGIN_COMPLETE), slot 43 (forward to the +0x388C8 handler), packet 0x17 (slot 99: sub-types 1-6), SetChatText (slot 48: ChatHandler:SetChatText), DisconnectFromServer (slot 40: packets 0x5F/0x71/0x07, player + session manager teardown, save options), RefreshJobBrowser (slot 49: HandlerJobBrowser:SetJobCount + per-job rows), ConnectToGateway (slot 38: recreate zone client, 60 s connect, connect-info / failure reporting), OnLoginFailed (slot 41: locked / unbound / already-linked with masked, URL-escaped account name), packet 0x41 (slot 97: object +0x38E68 create/destroy/entries, sub-types 1-4, 6-8, 11, 12), PresentJob (slot 50: HandlerJobBrowser:SetJob with 8 args + :PresentJob), Initialize (slot 31: world init, web browser, user options, WallOfData), packet 0x83 (slot 101: sub-types 0x08-0x26), Init (slot 2: ini + LocalConfig, options, state objects, SoeData driver, timers), DrawStatusOverlay (slot 103: godmode/hidden/spectator text), CreateAssetSystem (slot 78: [AssetDelivery] direct/indirect, manifest wait, loader budget), Update (slot 89: per-frame tick with profile laps at state+0xA10, fade, camera, render lock, loader sync), ShutdownSystems (slot 77: tears down ~90 global/client subsystems in order), ShutdownGame (slot 37: GuiOnSave, ~120 game systems, world flush, display), GiveTime (slot 9: 36-state login/connection/world-load state machine + frame bookkeeping), CreateAppServices (slot 76: ~90 app services, resource managers, world, audio, camera), WriteCrashInfo (slot 117: ~130 guarded crash-report entries), HandleInputActions (slot 90: ~45 "Generic" input actions - HUD/inventory/builder/map toggles, escape, quick chat, render distance, proximity voice channels, demo ReviveMe), packet 0x11 (slot 93: ~55 sub-types - stats, objectives, jobs, abilities, teleport, entity transforms, loyalty points, server cvars, character select), forwarding slots 1, 35, 44, 52, 59, 82, 83 (window title), 88, 95, 96 (group packets), 116, 118, 120 |
| Game | zone opcode dispatcher | DispatchZonePacket 0x1403fe210, fully rebuilt: all 168 built-in opcodes (SendSelfToClient 0x03, single-call routes to subsystems, ZoneDoneSendingInitialData 0x05, KickedFromServer 0x2F, stack-packet readers 0x25/0x3D/0x69/0xAA/0xAB/0xAF/0xB6/0xE6/0xEE, constructed packets 0x25/0x33/0x97/0xA8/0xAE/0xB0/0xB1/0xCB (0xB0 falls into 0xB1 on a failed read, as in the original), click-to-move reply 0x3E, 0xDE (its 0x200F8-byte object moved from the stack to the heap), begin zoning 0x0B, the 0xE3 sub-dispatcher incl. the matchresults CSV writer, reader-pattern packets 0x79/0xD6/0xD7/0xD8/0xDB/0xDC, 0xC5, id-list 0xD5, 0x35, localized message 0x43, 0x4F, race/checkpoint stopwatch UI 0x78, 0x44, 0x61, 0x65, loyalty info 0x7D, trial job expired 0x62, membership activated 0x40, timed kick 0x3F, server message 0x08, server shutdown + BadPackets.txt dump 0x2C, session details 0x30, ZoneDetails 0x16 (0x99 falls into it, as in the original), opcode-only 0xB9, login-failed shutdown 0x51, lazy-init routes, 0x32 value packet), plus the extension-handler default for the 78 table gaps and out-of-range opcodes; the original's fallthroughs (0x99 -> 0x16, 0xB0 -> 0xB1) are kept. `REBUILD_FUNCTION_WITH_ORIGINAL` exists for future partial rebuilds |
| Game | packet stream helpers | u8/u16 writers, tunnel-message header+byte(+chunk list) writers, packed-byte writer, chunk-list writer, channel-2 packet readers, {opcode,int} packet reader, pooled stream holder ctor |
| GameCommerce | marketing-data containers | two HashListMap instances: Remove, Clear, Rehash (templated over node layout) |

Total: 903 functions (872 hooked, 31 too small to hook).

Next: the rest of `UdpConnection` (internal GiveTime 0x140347360,
ProcessRawPacket 0x1403491e0, the big packet handler 0x140348390,

constructor 0x140345090), then the `UdpManager` constructor/destructor and
the SoeUtil DynamicMemoryPool it owns at +0x610.

Third-party code in the exe (PhysX/APEX, curl, Vivox, Steam API, MSVC CRT/STL)
is not rebuilt by hand: it gets identified and linked from the real libraries.
