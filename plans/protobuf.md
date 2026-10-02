# Protocol Buffers for zForth and ZGo

This is the plan for protobuf support on BlueStreak devices running zForth
and ZGo: a wire codec, typed messages generated from the server's schema, the
canonical state hash, and the RPC envelope. **Status: design agreed, nothing
implemented yet.**

Read `AGENTS.md`, `LANGUAGE.md` and `ZGO_PLAN.md` section 0 first. This plan
follows their rules: no allocation, cheap checks, new Forth words in
`forth/`, ask before committing.

## 0. Context

### 0.1 How the existing firmwares do it

Three firmwares speak the same protocol. This plan is based on them:

| Firmware | Where | Protobuf implementation |
| --- | --- | --- |
| Diffuser (C) | `~/work/diffuser-firmware/bslib`, `YC_05_0003_40_APP/app/bslib` | nanopb 0.4.9-dev, static structs, a `.options` file for sizes |
| scentgate (MicroPython) | `~/work/bsplatform/scentgate/development/bsapp` | vendored minipb (`Common/minipb.py`), schema tuples, dicts |
| plug/diffuser (Tasmota Berry) | `~/work/bsplatform/tasmota/development/bluestreak` | no wire codec: sends the same messages as JSON; `berry/pb.be` only hashes |

What they have in common, and what this plan keeps:

- **The schema comes from the server.** `bs-plat/lib/proto/device-v1.proto`
  holds the RPC envelope; `pblib.js:generate_proto` adds `Settings` and
  `Readings` for each device type from database property definitions. The
  server can download a device type's schema as protobufjs JSON or as
  `.proto`, and the download **already includes the base RPC messages**.
- **Everything is driven by a field table**: nanopb's `*_FIELDLIST` macros
  and `bs_forall_settings`, minipb's schema tuples, Berry's schema maps. The
  same table drives encode, decode, merge, `field_ids` filtering and the hash.
- **The envelope**: `RPC_Cmd` / `RPC_Reply` = optional `RPC_Header`
  (`device_id`, `seq`, `subseq`, `sint32 status`, `device_type`,
  `device_type_alias`) plus a `oneof` with the command or reply. Commands and
  replies use matching tags (`get_settings` = 2 in both).
- **Every field is proto3 `optional`**, so each field has explicit presence.
- **Field number ranges** (constants in `pblib.js`):
  - below 9000: synced, part of the hash;
  - 9000 to 19999: unsynced (one-shot commands, status such as `connected`),
    left out of the hash and of default `get_*` replies;
  - 20000 + N: `optional bool has_<name>` for repeated field N, because proto3
    cannot tell an absent list from an empty one (`HAS_FIELD_START_ID`);
  - 40000 and up: server-only properties (`MAX_DEVICE_PROPERTY_ID`), not sent
    to devices.
- **Merge**: `set_settings` applies only the fields present in the message.
  A repeated field is replaced only when its `has_` flag is true; `has_`
  true with the list missing means an empty list.
- **The canonical hash** (section 6) lets the server compare state by hash
  (`get_datahashes`) instead of fetching it.
- **Transports**: MQTT carries raw protobuf (`<base>/s/rpc_cmd`,
  `<base>/d/rpc_cmd`, `<base>/s/rpc_reply`), optionally wrapped in
  AES-128-CTR + HMAC-SHA256; HTTP carries it base64 in JSON; LoRa carries it
  in hex chunks of about 118 bytes. Transports are outside this plan, which
  only needs them to hand over and accept whole messages as bytes.

### 0.2 Decisions made (2026-10-02)

- **`pblib.js` is the canonical reference** for the hash. Section 6
  specifies the hash as `pblib.js` computes it (including two fixes made
  separately in bs-plat), and this project's tests check against that
  specification, not against bs-plat.
- **Binary protobuf on the wire.** JSON is not implemented, but the design
  keeps it possible where that is cheap (section 8).
- **A second importable package, `pb`**, next to `dev`.
- **The generator reads protobufjs JSON** (the server's download format).
- **Size limits come from an options file and from inline options; the
  options file wins** (section 3.2). Server-side limits cannot be trusted
  today: the backend uses `array_length` only as a flag.
- **A message with a value that doesn't fit is rejected as a whole**: no
  partial apply. Truncating or dropping a field would leave the device's
  hash different from the server's while the device acts on partly applied
  settings. A rejected message still leaves the hashes different, so the
  server resends it, but only once per check-in, and **no server change is
  needed** (section 5.4).

### 0.3 Wire features needed

From the schemas in use and the types `pblib.js` can generate:

- scalar types: `uint32`, `sint32`, `uint64`, `sint64`, `float`, `double`,
  `bool`, `string`, `bytes` (the keys of `base_types` in `pblib.js`);
- nested messages, repeated scalars (packed, the proto3 default), repeated
  messages, repeated strings;
- `oneof` (the envelope, `CmdRelayMessage.hdr`);
- skipping unknown fields of every wire type (0, 1, 2, 5), so old devices
  accept messages from newer schemas.

Not needed: enums, maps, groups, `int32`/`int64`, `fixed*`/`sfixed*`,
extensions. The generator rejects a schema that uses them, naming the field.

## 1. Goals and principles

- **One field table per message drives everything**: encode, decode, size,
  merge, filter, hash, and later JSON. Generated per-message code is limited
  to the struct, accessors and the table.
- **Typed ZGo messages.** Scripts and firmware see structs with accessors,
  not maps; type errors are caught at compile time.
- **The shared runtime is in ROM**, once, in Forth (`forth/pb.zf`), so every
  program on the device uses the same code.
- **No allocation, no unions.** Messages are static structs with fixed-size
  arrays, as nanopb does with `.options`.
- **Byte-exact with the server**: the encoder's output decodes in protobufjs,
  and the hash follows the specification in section 6, taken from
  `pblib.js`.
- **Cheap on the ESP32-C3.** Integer-only work (floats are copied as their
  bits); measure before moving anything to C.

## 2. Architecture

```
server schema (protobufjs JSON) + device.options
        |
        v
zgoc pbgen  ------------------>  schema_pb.zgo   (package main: structs,
        |                                         accessors, field tables,
        |                                         typed bindings)
        v
program .zgo files  ---- import "pb" ----> zgo/pb/pb.zgo  (declarations,
        |                                                   //zf:forth)
        v
zgoc build / image  -------->  Forth  ---- calls ---->  forth/pb.zf (ROM)
                                                          |
                                                          v
                                                 sha256 syscalls (host)
```

Layers, bottom up:

1. **`forth/pb.zf`**: wire primitives and the table walkers (section 4).
2. **Host syscalls**: SHA-256 only (section 4.4).
3. **`import "pb"`**: a ZGo package of declarations bound to `pb.zf` words,
   like `dev` (section 5.1).
4. **`zgoc pbgen`**: generates the typed messages from the schema
   (section 3).
5. **The RPC layer** in the core firmware, written in ZGo (section 7).

## 3. The generator: `zgoc pbgen`

```
zgoc pbgen -schema scentfamilydiffuser.json [-options device.options]
           [-messages RPC_Cmd,RPC_Reply,...] [-names] -o schema_pb.zgo
```

It reads protobufjs JSON, applies size options, and writes one `.zgo` file
in `package main` that is compiled together with the program. It lives in
the `zgo/` module and reuses the compiler's type layout code (`types.go`,
32-bit sizes, alignment up to 4), so the offsets in the field tables are
exactly the ones the compiler uses.

### 3.1 Reading the schema

- `nested` holds every message; `fields` maps names to `{type, id, rule?,
  options?}`; `oneofs` lists real and synthetic oneofs.
- **Synthetic oneofs** (`"_header": {"oneof": ["header"]}`) are how protobufjs
  marks proto3 `optional`; they become presence bits, not oneofs. A real
  oneof has a name without the leading `_` and usually several members.
- Fields with tag 40000 and up are dropped, with a note.
- `has_<name>` fields at tag N + 20000 are linked to repeated field N (they
  stay ordinary bool fields on the wire, but merge uses them; section 5.4).
- `-messages` limits generation to the messages a program needs, plus
  everything they reference (Tasmota's `jq` pruning, done by the generator).
- Anything outside section 0.3 is an error that names the message and field.

### 3.2 Size limits

Every `string`, `bytes` and repeated field needs a limit. There are two
sources:

- **Inline options in the schema**: nanopb's `(nanopb).max_length`,
  `max_size`, `max_count`, which protobufjs keeps in the field's `options`
  (`"(nanopb).max_length": 512`).
- **An options file** in nanopb's format (`Settings.schd max_length:512`,
  glob patterns allowed), so the same file also works for C firmware.

**The options file wins.** This is the reverse of nanopb, where inline
options are applied last. The schema belongs to the server and says what
values it may send; the options file belongs to the device and sets its
memory budget, so the device gets the last word. To keep that safe:

- the generator prints every field where the file overrides an inline value;
- an override **above** the inline value is a warning (it only wastes RAM);
- a field with no limit from either source is an **error**: the generator
  never guesses a size;
- a field whose file limit is below what the server sends causes rejected
  messages at run time (section 5.4); the generator's report is where that
  shows up first.

Today server limits cannot be trusted, so in practice every device project
has an options file. Inline options are supported so that the server can
start emitting them later (an `array_length`-style column used as a real
maximum) without a device-side change.

ZGo-only options (for example storing a oneof member lazily, section 3.4) go
in the same file with a `zgo_` prefix. Whether nanopb's parser tolerates
unknown option names, or whether these need a separate file, is checked
when they are added.

### 3.3 Generated messages

For each message, the generator emits:

- **A struct** with private storage:
  - a presence bitset, one bit per field (`has [N]uint32`);
  - scalars in their ZGo types (`uint32`, `int32` for `sint32`, `uint64`,
    `int64`, `float32`, `float64`, `bool`);
  - `string`/`bytes` as `[max]byte` plus a length (`uint8` or `uint16`);
  - repeated fields as `[max_count]T` plus a count;
  - nested messages inline;
  - a oneof as all its members inline plus a `which` field holding the
    member's tag. ZGo has no unions, so a oneof costs the sum of its
    members; section 3.4 covers the envelope, where that matters.
- **Accessors modeled on Go's protobuf Opaque API** (`GetX`, `SetX`, `HasX`,
  `ClearX`), which agents already know and which keep presence correct
  because storage is private:
  - `GetX() T` returns the zero value when absent; strings and bytes return a
    view of the storage (`string(m.x[:m.xLen])`), as `string(buf[:n])` does;
  - `SetX(v T)` for scalars; `SetX(v string) bool`, `SetX(v []byte) bool`
    and `SetX(v []T) bool` for strings, bytes and repeated fields, returning
    false (and changing nothing) when `v` does not fit;
  - repeated fields also get `XLen() int`, `XAt(i int) T` and
    `AppendX(v T) bool`;
  - nested messages: `GetX() *Sub` points at the inline storage, and
    `MutableX() *Sub` also sets presence;
  - oneofs: `WhichY() int32` returns the set member's tag, and setting a
    member clears the others.
- **Field-number constants**: `Settings_Fan = 4`, and so on.
- **The field table** (section 4.2) as a package-level constant array, so it
  is part of the ROM image.
- **Typed bindings and methods.** ZGo has no interfaces or `unsafe`, so a
  generic word cannot take "any message". `//zf:forth` only pushes the
  arguments, though, so the generator declares one typed binding per message,
  all bound to the same generic Forth word:

  ```go
  //zf:forth pb-encode
  func pbEncodeSettings(t []uint32, m *Settings, buf []byte) int

  func (m *Settings) Marshal(buf []byte) int { return pbEncodeSettings(settingsTable[:], m, buf) }
  ```

  The methods are `Marshal(buf []byte) int`, `Unmarshal(b []byte) int`
  (protobuf's merge), `Merge(b []byte) int` (the server's settings merge,
  for messages with `has_` fields), `Size() int`, `Reset()` and
  `Hash(out []byte)`, each forwarding to a generic word (section 5.4).
- **Optionally (`-names`), a name table**, separate from the field table and
  referenced only by the words that need it (debug printing; JSON later).

### 3.4 The envelope and large oneofs

`RPC_Cmd` holds every command in its `oneof`, including `data_update`, which
embeds the whole `Readings`. Inline, that is several kilobytes (nanopb's
union is already about 1.9 KB for the diffuser). Two ways to avoid it, both
supported:

- **Read the envelope with the `pb.Consume*` functions** (section 5.2): decode the header,
  find the oneof's tag and its bytes (a view into the input, no copy), check
  the tag against the transport's allow-list, then decode just that payload
  into its own static struct. `bs_app_rpc_get_cmd_request_tag` already does
  this check before a full decode.
- **A `zgo_lazy` option for a field**: the generated struct stores the
  member as a `[]byte` view of the input instead of a decoded struct, and
  `Unmarshal` validates it only as far as the wire format (section 5.4).

The RPC layer (section 7) uses the first; the second is there for other
messages with large members.

## 4. zForth: `forth/pb.zf`

A new file (it is a separate set of words; `AGENTS.md`), loaded after
`zgo.zf` wherever ZGo programs run (`zgoc run`, `zgoc image` and device ROM
images), so it is in ROM once for every program. Written in Forth
first; any word that profiling on the ESP32-C3 shows to be hot can later
become a syscall on the next free ID after the SHA-256 ones (section 6.1);
syscalls don't renumber primitives, so no ROM image needs regenerating.

### 4.1 Wire primitives

On `( buf len pos )` buffers in the dictionary:

- varints: put and get, one-cell (`uint32`) and two-cell (`uint64`);
  getters check for truncation and overlong (more than 10 bytes) varints;
- zigzag: `sint32` and `sint64`;
- tags: put and get `( field-number wire-type )`;
- fixed32 and fixed64: little-endian, copying the bits (so `float` and
  `double` need no float code);
- length-delimited: put a length and bytes; get a view `( addr len )`;
- skip a field of any wire type.

Errors leave a failure flag rather than aborting, so a bad message from the
network never resets the stacks (a `zf_abort()` outside `zf_eval()` reboots
an ESP32).

### 4.2 The field table

A packed array of `uint32` per message, made by the generator and read by
the walkers. Per field (exact bit layout decided in phase 1):

- field number and kind (one of the nine scalar types, or message);
- flags: repeated, packed, oneof member, lazy, unsynced (tag 9000 to 19999),
  and has-flag link;
- offset of the value in the struct, element size, limit (max length or
  count), offset of the length or count, presence bit index;
- for messages: the index of the sub-message's table, so walkers recurse;
  for oneof members: the offset of the `which` field.

Fields are listed in field-number order, which is also the hash order, and
the table header carries the message's struct size and field count.

### 4.3 Walkers

Generic words that take a table and a struct address:

- **size**: the encoded size, used for buffer checks;
- **encode**: writes present fields in field-number order. Repeated scalars
  are packed. A nested message is written after a one-byte length, and moved
  up with `move` when its length needs more bytes (no sizing pass, and the
  output stays byte-identical to other encoders);
- **check**: walks the input and verifies every field fits its limit and
  every nested message is well-formed, without writing (section 5.4);
- **decode**: merges into the struct, following protobuf rules: a scalar
  that appears twice keeps the last value, a nested message merges,
  repeated elements are added (packed and unpacked forms both accepted);
  unknown fields are skipped. It also records which fields changed, as a
  bitset the caller can read;
- **hash**: section 6;
- **visit**: calls back per present field for the `field_ids` filter, and
  later the JSON writer.

## 5. ZGo: `import "pb"`

### 5.1 The package

`zgo/pb/pb.zgo`, embedded in the compiler the way `zgo/dev` is, and
resolved by the same custom importer (`compiler.go:226`); `validate.go`
accepts `"pb"` next to `"dev"`. It contains only declarations bound with
`//zf:forth` and constants, so the compiler needs no support for compiling
another package's code. Things to confirm in phase 2:

- constants from an imported package fold as they do for `dev`;
- the package declares no types: `go/types` would accept them, but types
  crossing packages are untested in the compiler, and the API below doesn't
  need them;
- compat tests get a Go stand-in for `pb` (as `devStandIn` is for `dev`),
  built on `google.golang.org/protobuf/encoding/protowire`.

### 5.2 Low-level API

Modeled on `protowire`, whose `Consume*` functions are already slice-based
and allocation-free; `protowire`'s `Append*` functions allocate, so
`Put*` versions write at a position instead:

```go
func ConsumeTag(b []byte) (num int32, typ int32, n int)   // n < 0: error
func ConsumeVarint(b []byte) (v uint64, n int)
func ConsumeBytes(b []byte) (v []byte, n int)             // a view, no copy
func ConsumeFixed32(b []byte) (v uint32, n int)
func ConsumeFieldValue(num int32, typ int32, b []byte) (n int)  // skip
func DecodeZigZag(v uint64) int64
func PutTag(b []byte, num int32, typ int32) int           // bytes written, -1 if no room
func PutVarint(b []byte, v uint64) int
func PutBytes(b []byte, v []byte) int
const VarintType, Fixed32Type, Fixed64Type, BytesType = 0, 5, 1, 2
```

The RPC layer's envelope reader and any hand-written message handling use
this API. Typed messages (section 3.3) are the normal way to work with
`Settings` and `Readings`.

### 5.3 Typed API

The generated methods (section 3.3). A typical handler, decoding straight
into the live settings (section 5.4):

```go
var settings Settings // the device's current settings
var reply [256]byte

func onSetSettings(payload []byte) int {
	if settings.Merge(payload) < 0 {
		return -1 // rejected; settings unchanged
	}
	if settings.HasFan() {
		fanSet(settings.GetFan())
	}
	return settings.Marshal(reply[:])
}
```

### 5.4 Decoding is all or nothing

A message is rejected as a whole when any value does not fit (a string over
its `max_length`, more elements than `max_count`), when a nested message is
malformed, or when the input is truncated. `Unmarshal` and `Merge`
therefore run two passes:

1. **check**, which validates the whole message against the table without
   writing anything;
2. **decode**, which merges it into the struct and cannot fail, because the
   check pass has already seen every byte.

This means a message can be decoded straight into the live state (for
example the device's current `Settings`) with no scratch copy, and a
rejected message leaves it untouched. Both return the bytes read, or a
negative error code saying why the message was rejected.

Merging settings follows the server's rules (0.1): only present fields are
applied; a repeated field `X` is replaced, not appended to, when
`has_X` is true, and cleared when `has_X` is true and `X` is absent. That is
a variant of protobuf's own merge, so it is a separate walker mode behind
`Merge`, used by the RPC layer for `set_settings`.

When the check fails, the RPC layer replies with the device's **current**
settings hash (unchanged, because nothing was applied) and a non-zero
`RPC_Header.status` (section 7).

How the server handles that today (bs-plat `development`, checked
2026-10-02), with no server change:

- **`status` is ignored.** No reply path in `services/device_rpc.service.js`
  reads `header.status`. It costs nothing to send, and it is there for
  whenever the server starts reading it.
- **The `set_settings` reply never triggers a resend.** Its hash is stored as
  `last_known_device_hash` (`applySettingsAcknowledgement` in
  `shadow.service.js`), called with `skip_event` set, so no
  `shadow.settings.needsSync` is emitted. The stored hash then differs from
  the shadow's, which is how the mismatch shows up on the server.
- **Resends come from check-ins.** A `data_update` or `get_datahashes`
  reply whose `settings_hash` is not a prefix of the shadow's hash emits
  `needsSync`, and `sync_settings` sends the full settings again. A
  rejected message is therefore resent once per check-in, not in a tight
  loop. Any hash mismatch behaves this way, so rejecting is no worse than
  truncating would be, and leaves the device's settings consistent.

The real fix for an oversized value is a larger limit in the device's
options file. The generator's override report (section 3.2) is where a
limit below what the server sends shows up first.

## 6. The canonical hash

The hash as `hash_pb_object` in `pblib.js` computes it, computed from the
struct, not from wire bytes. This section is the specification the device
implements and tests against. For each message, over the fields in
field-number order, skipping fields with tag 9000 or above unless
`include_unsynced` is set:

| Field | Bytes fed to SHA-256 |
| --- | --- |
| `uint32` | 4 bytes little-endian |
| `sint32` | 4 bytes little-endian, two's complement |
| `uint64`, `sint64` | 8 bytes little-endian |
| `float` | 4 bytes, IEEE single, little-endian |
| `double` | 8 bytes, IEEE double, little-endian |
| `bool` | 1 byte, 0 or 1 |
| `string`, `bytes` | the raw bytes, no length |
| absent scalar | the zero value of its type (an empty string adds nothing) |
| nested message | the nested message's fields, recursively (absent: all zero values) |
| repeated field | each element in order, as above; absent or empty adds nothing |

`include_unsynced` is passed down into nested messages, as `pblib.js`
does (the server never sets it today, so nested unsynced fields are left
out).

Notes for the implementation:

- Storage for an absent field must hold the zero value (`ClearX` zeroes it),
  so the hash can read the struct without looking at presence bits, except
  for repeated fields, whose count is already zero.
- `float` NaN: Node's `writeFloatLE(NaN)` writes `0x7FC00000`; the device
  hashes whatever bits it stores. Not a problem unless a NaN is ever stored.
- The hash has no field separators, so values can slide between neighboring
  fields (`{b:"ab", c:""}` and `{b:"a", c:"b"}` hash alike). That is a known
  weakness of the canonical format; changing it would change every hash on
  every device, so it is kept.
- The result is reported as hex, cut to `req_hash_length` bytes (default 16),
  as the firmwares do today.

### 6.1 SHA-256 syscalls

The ESP32-C3 has a SHA accelerator, and the MQTT encryption needs SHA-256
(HMAC) anyway, so hashing is a host service. Proposed: `sha-begin`,
`sha-update ( addr len -- )`, `sha-end ( out -- )`, on new IDs from 136
up, with one hash in progress at a time. The Linux host implements them
for tests. `SYSCALLS.md` gets the new rows.

## 7. The RPC layer

The device platform has three levels (decided 2026-10-02):

1. **The host**: Arduino or ESP-IDF code providing the low-level services
   and syscalls.
2. **The core**: zForth and its ROM image, mostly written in ZGo. It talks
   to the backend, sends and answers RPC messages, reads sensors. The host
   and the core are built and shipped together, as one firmware per device
   type (they may be split later, but not now).
3. **Scriptlets**: optional end-user programs that change behavior in small
   ways. The device works without them.

More levels can be added later, for example ZGo library packages imported
by higher-level ZGo code.

For protobuf this means:

- **Everything in this plan is in the core**: `pb.zf`, the generated schema
  code (compiled into the core's ZGo program), the envelope, transports,
  hashing, merge and the `get_*`/`set_*` handlers. The core reports changed
  settings to the rest of the device code through the changed-field bitset
  (4.3).
- **The host and the core always match**, so syscall numbers, `pb.zf` words
  and the field table layout can change together, with no compatibility
  between versions to maintain.
- **A device type's schema is fixed when its firmware is built.** Fields a
  newer server schema adds are skipped as unknown fields until the next
  firmware build, as with today's firmwares.
- **Scriptlets don't use `pb`**, at least for now.
- **If ZGo gains library packages**, the generated schema code could become
  a package of its own instead of files compiled into `package main`.

What the core does with the pieces above:

- **Incoming `RPC_Cmd`**: read the header and the oneof's tag with
  the `pb.Consume*` functions; ignore the command if `device_id`, `device_type` or
  `device_type_alias` is set and doesn't match; check the tag against the
  transport's allow-list; decode the payload into its own struct.
- **`get_settings` / `get_readings`**: encode with the field filter
  (`field_ids`, unsynced fields only when asked) and the hash.
- **`set_settings`**: merge into the live settings (section 5.4); reply with
  the new hash; if the message was rejected, reply with the unchanged hash
  and a non-zero `status` (the server ignores `status` today); hand the
  changed-field bitset to the device code.
- **`get_datahashes`**, **`data_update`** (check-in, and the slimmed
  `connected` message used as the MQTT last will), the firmware package
  commands: as the diffuser does today (`bluestreak.c`).
- **The reply** echoes `header.seq`; nothing is sent when the reply has no
  header.

This is ordinary ZGo; it is listed here so that the `pb` API is checked
against a real user. Its own design (transports, encryption, scheduling)
belongs in a separate plan.

**Outside this plan:** how scriptlets reach the core's state. Programs
loaded one after another only see each other's exported words (`z-Name`),
so a scriptlet has no typed view of the core's `Settings` or `Readings`.
That is a general ZGo question about scriptlets, for their own plan.

## 8. Room for JSON later

Not implemented now. What keeps it cheap later:

- the RPC layer works on decoded structs, never on wire bytes, so the codec
  can change underneath it (the hash already works this way);
- the field table records the full type (string versus bytes, `sint32`
  versus `uint32`, 64-bit), which JSON needs and protobuf alone would not;
- field names come from the optional name table (`-names`);
- a JSON writer is another walker mode over the same tables (`visit`);
- topic names (`rpc_cmd` versus Tasmota's `js_cmd`) are transport settings.

Which JSON dialect (protobufjs `keepCase` names as Tasmota uses, or proto3's
canonical JSON with camelCase and 64-bit values as strings) is decided only
when JSON is needed.

## 9. Testing

Under `make test`, as `AGENTS.md` requires:

- **zForth suites** (`tests/suites/`): every wire primitive, edge cases
  (10-byte varints, truncation, overlong varints, `sint` extremes), on the
  RAM, ROM and UBSan builds.
- **Wire compatibility with Go**: compat programs in `zgo/testdata/compat`
  that encode and decode through `pb`, compared with real Go using the
  `protowire` stand-in. For typed messages, golden encodings produced by
  `protoc`-generated Go code from a test schema, checked byte for byte both
  ways.
- **Hash tests against section 6**: a small Go implementation of the
  section 6 specification (in the test code, using `crypto/sha256`) gives
  the expected hashes, and the device's hash must match it. The cases cover
  every scalar type, absent versus empty repeated fields, nested and absent
  nested messages, unsynced fields with and without `include_unsynced`,
  and the real device schemas (`plugmonitorgw.json`,
  `scentfamilydiffuser.json`, copied into `zgo/testdata/pb/`). A few
  expected values are written out by hand, so the Go implementation is
  checked too. Nothing depends on bs-plat or Node.
- **Generator tests**: option precedence and its report, missing limits,
  rejected schema features, synthetic versus real oneofs, `has_` linking,
  and offsets matching the compiler's layout.
- **All-or-nothing decoding**: oversized and malformed messages leave the
  struct byte-for-byte unchanged.
- **An example** under `examples/` that decodes a `set_settings`, merges,
  hashes and replies, run in both run and image modes.

## 10. Phases

1. **Wire primitives** in `forth/pb.zf`, with zForth tests; the field
   table layout.
2. **`import "pb"`**: compiler support for a second package, the low-level
   API, the Go stand-in and compat tests.
3. **`zgoc pbgen`**: schema reading, options and their precedence, structs,
   accessors, tables, bindings; generator tests.
4. **Walkers**: size, encode, check, decode/merge, with golden encodings
   both ways.
5. **Hash**: SHA-256 syscalls (`SYSCALLS.md`), the hash walker, tests
   against section 6.
6. **RPC example** and documentation: a `LANGUAGE.md` section for `pb`, and
   this plan updated to match what was built.

## 11. Open decisions

1. **Field table layout** (section 4.2): fixed in phase 1, from what the
   walkers need.
2. **`zgo_` options**: in the nanopb options file or a separate file
   (section 3.2).
3. **Rejected messages**: the `status` values to reply with. The server
   ignores them today (section 5.4); making it read them, for example to
   stop resending, would be a later server change.
4. **Server-side limits**: when the backend can emit trustworthy
   `(nanopb)` options, device projects can drop most of their options
   files. That is a bs-plat change, not part of this plan.
