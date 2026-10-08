# Format-2 fee estimator cache

`fee_estimates_format2.dat.gz` is an empty fee estimator cache written on a
clean regtest shutdown by PR #193 (`upstream/fee-estimator-60s`, head
`961d1fd0c`), client version 500. That build writes `FEE_ESTIMATES_FORMAT = 2`
with the 12x10 short horizon. No transactions or public network were used.

Decompressed size: 247985 bytes.
SHA-256: `23b4ec927e8459f26d184c15c9fd06f39001bd5a50e8cd2758e5d90fda0f3946`.

To make an equivalent file, build `961d1fd0c`, start `btqd -regtest` with an
empty datadir, stop it, and gzip `regtest/fee_estimates.dat`. The format and
bucket geometry will match; a byte-for-byte match has not been checked.

The functional fee test checks nonfatal rejection of this incompatible geometry,
then format-3 write/read. The C++ estimator regression separately checks learned
short-target estimates survive format-3 serialization exactly.
