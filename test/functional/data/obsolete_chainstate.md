# Obsolete chainstate schema fixture

`obsolete_chainstate.json` encodes three LevelDB files as hex. The database has
exactly one key: byte `0x63` (`DB_COINS` in `src/txdb.cpp`) followed by 32 zero
bytes, with an empty value. `CCoinsViewDB::NeedsUpgrade()` rejects this key
namespace before reading a coin value. This is deliberately a schema marker,
not a valid old coin record or a database attributed to a historical release.

Generated using this repository's vendored LevelDB at `475473e817cffe3d88613d9dc6c46322a90e591b`:

```cpp
#include <leveldb/db.h>
#include <cassert>
#include <memory>
#include <string>
int main(int argc, char** argv) {
    assert(argc == 2);
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* raw = nullptr;
    assert(leveldb::DB::Open(options, argv[1], &raw).ok());
    std::unique_ptr<leveldb::DB> db(raw);
    assert(db->Put(leveldb::WriteOptions(), std::string("c") + std::string(32, '\0'), "").ok());
}
```

After a normal repository build, compile with:

```sh
g++ -I src/leveldb/include /tmp/make-obsolete-chainstate.cpp \
  src/leveldb/.libs/libleveldb.a src/crc32c/.libs/libcrc32c.a \
  src/crc32c/.libs/libcrc32c_sse42.a -pthread \
  -o /tmp/make-obsolete-chainstate
/tmp/make-obsolete-chainstate /tmp/new-obsolete-chainstate
```

Serialize every file except `LOCK` and `LOG` as `{filename: contents.hex()}`.
Use a fresh output directory. The test copies the fixture into a stopped,
disposable regtest node, requires the exact unsupported-format error, and
checks that reindexing restores the original tip and complete UTXO commitment.
The compile command above is for the tested x86-64 build with SSE4.2 support.
