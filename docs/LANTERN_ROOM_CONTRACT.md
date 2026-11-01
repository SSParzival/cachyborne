# Lantern room compatibility investigation

Inspected source: Lantern Engine `e6b943dd969d99561545315f3d801f10e4a6cf8d`,
using the local `audit/lantern` ref. This document describes protocol differences;
**Cachyborne does not implement Lantern rooms or advertise cross-client compatibility.**

The current `gpu/bbnet20` module implements shadPS4 0.20's shadNet/NP services,
The Hunter's Dream HTTP provider and BBSC game hooks. Lantern's room transport
is a separate service, not a replacement version string for that module.

| Contract | Lantern source | Integration implication |
| --- | --- | --- |
| WebSocket upgrade | `engine/gpu/bbnet/room/room_ws.cpp`, `room_host.cpp` | `X-Lantern-Room: 16`; host rejects versions below 16 with HTTP 426. Successful WebSocket upgrade alone is insufficient. |
| Room identity | `room_host.cpp` | 64 hexadecimal characters; optional 32-character hexadecimal token. Host compares room ID in constant time. Preserve identity and authentication semantics. |
| Session state | `room_link.cpp` | `welcome`, member slots, host designation, disconnect/reconnect and reliable/lossy destinations. A socket connection is not a joined game. |
| RPC | `room_service.cpp` | Binary opcodes 1/2/3: request/reply/event; request and reply include a little-endian 32-bit call ID before JSON. Maintain bounded outstanding calls and cancellation. |
| Game traffic | `room_service.cpp` | Opcodes 4–28 include datagrams, batching, vitals, travel, progress, dungeon/chalice state, portraits, bundles, bring, voice, boss lots/trophies, pieces, party calls, flags, ping/pong, warp/lamp ask/answer and staged travel. Unknown traffic cannot be interpreted as NP packets. |
| Limits | `room_service.cpp` | Batch 16,000 bytes, bundle 60,000, portrait 128 KiB, piece payload 1,200 with 4-byte header, voice backlog 2,048 bytes, ping name 32 bytes. Validate sizes before decoding or allocating; these are not sufficient to define every packet's schema. |
| Guest world state | room service and game hooks | Travel/progress/flags, ownership and summon state must agree before applying effects. Preserve local save isolation, local trophy ownership and offline behavior. |

A complete bridge needs a transport/session implementation, bounded parsers for
all negotiated message types, a mapping from Lantern ownership/travel/progress
semantics to the guest hooks, disconnect recovery and integration with the
launcher. Packet round trips in a local codec would only validate that codec.
They would not establish handshake, co-op or save integrity with Lantern.

No client Lantern or known route replay was supplied. Local copied-save testing
is authorized and available. Cross-client acceptance needs two isolated clients,
including a client at the inspected Lantern revision, covering join, summon,
combat, lamp travel, host departure/reconnection and independent save hashes.
No public rooms were contacted and no network compatibility switches were added.

The reviewed room implementation is marked GPL-3.0-or-later; the imported shadow
compute shader is separately marked GPL-2.0-or-later. Room sources were not copied
into the GPL-2.0-or-later native host integration in this change.
