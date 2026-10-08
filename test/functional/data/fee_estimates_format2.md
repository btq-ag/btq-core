# Format-2 fee estimator cache

`fee_estimates_format2.dat.gz` contains an actual empty cache written on clean
regtest shutdown by BTQ commit `c27cf1602` (locally integrated PR193), client
version 500. No transactions or public network were used.

Decompressed size: 247985 bytes.
SHA-256: `23b4ec927e8459f26d184c15c9fd06f39001bd5a50e8cd2758e5d90fda0f3946`.

The functional fee test checks nonfatal rejection of this incompatible geometry,
then format-3 write/read. The C++ estimator regression separately checks learned
short-target estimates survive format-3 serialization exactly.
