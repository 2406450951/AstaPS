# 7.2 protocol generation

`protocol/7.2/protocol.desc` is the merged 7.1 server descriptor set (base, `compat` overrides and the TPS renames) with every message and field renumbered to 7.2. Names, types and file layout are unchanged, so server code compiles against it as before. `protocol/7.2/compat` is empty; 7.2 corrections go there the same way as in 7.1.

The descriptor set and the CmdIds in `PacketOpcodes` come from `tools/protocol/remap/remap_71_to_72.py`:

```
python3 -I tools/protocol/remap/remap_71_to_72.py \
    build/generated/protocol/7.1/server.desc \
    <kitkat 7.1.0 Deobfuscated.proto> \
    <Recovered72.nt.proto> \
    src/main/java/emu/grasscutter/net/packet/PacketOpcodes.java \
    protocol/7.2/protocol.desc report.json
```

It bridges through the kitkat 7.1.0 dump:

1. A server message is matched to its 7.1 dump message by its 7.1 CmdId, then by name, then through a parent field of the same 7.1 number.
2. A 7.1 dump message is matched to its 7.2 message by name, then through a parent field of the same name.
3. Each server field takes the 7.1 dump name found at its 7.1 number, and the 7.2 number of that name. Names are compared ignoring case and underscores, since 7.2 writes oneof arms in CamelCase. In messages whose named fields all kept their numbers, obfuscated fields keep theirs too.
4. Fields with no 7.2 number are parked at 20000 and up, so they are not read or written.

`WindSeedType1Notify` is mapped explicitly to `_PlayerNormalLuaShellNotify` (the UID watermark): CmdId 21002, payload field 15.

`ProtocolJavaGenerator` applies its 7.1 TPS renames only to `protocol/7.1`, because the 7.2 set already carries them.

What is still missing is listed in `docs/protocol-7.2-gaps.md`.
