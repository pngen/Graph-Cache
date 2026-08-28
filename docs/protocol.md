# Distributed Protocol

The control-plane protocol frames messages over TCP with a 4-byte little-endian
length prefix and a hard max frame size. Messages carry the coordinator epoch,
cache generation, worker id, worker boot id, graph generation, capture attempt,
replay attempt, request/dispatch identity, and a structured status.

The decoder strictly validates the protocol version, message type, field bounds,
and the payload length; it rejects malformed, truncated, zero-length, and
over-sized frames. The coordinator rejects stale epoch, stale worker boot, stale
cache generation, stale graph generation, obsolete capture attempts, duplicate
capture completion, completion after invalidation, and wrong base generation.