# Fixtures

Raw packet payloads, one file per packet.

    <server>/<direction>/<packet id>/<name>_<version>.bin

The packet id is lowercase hex with no prefix. The version matters because the
wire format changes between builds, so carry it whenever the payload can be
traced back to a session. A few older files predate that rule and have no
suffix. `_scrubbed` means player data was replaced before the file was
committed.

`fixture` gives you the bytes and `fixtureBuffer` wraps them for a codec that
reads from a buffer. Both live in the `common.test` module.

```kotlin
val buf = fixtureBuffer("game/s2c/9b/monster_page_32710.bin")
GtlSearchPagePacketCodec.read(buf)
buf.remaining() shouldBe 0
```
