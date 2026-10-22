# Launch parameters

This branch is the mainnet line. `master` still builds the testnet that is running today. Do not start a public node from this branch until that is a deliberate launch.

Both blocks use bits `0x1e0377ae`, version 1, one output of 5 BTQ, and the standard unspendable genesis pubkey. `nMinimumChainWork` is one genesis proof, `0x49d414`.

Subsidy in `GetBlockSubsidy` is 5 BTQ, halved every 2,100,000 blocks. The nominal cap before the halving shift drops sats is 21,000,000 BTQ. The whitepaper check is still open.

## Mainnet

| Field | Value |
| --- | --- |
| Message | `BTQ mainnet genesis 22/Oct/2026` |
| nTime | `1792627200` (2026-10-22 00:00:00 UTC) |
| Nonce | `505727` |
| Hash | `000002e4f1ad9a3f2321555ef6168d48295af74ec0921b69a3a76cec17b0e4f2` |
| Merkle root | `0bbd7929e39fd86a74157ebda9f3ecf8c018216951d7aacd0c0102ba061ef9d3` |
| Magic | `f1b2a3d4` |
| P2P / RPC / onion | 9333 / 8332 / 8334 |
| HRP | `qbtc`, Dilithium `dbtc` |
| LWMA | height 1, window 144 |
| Dilithium and P2MR | height 1 |
| Min difficulty | off |

## Testnet on this branch

| Field | Value |
| --- | --- |
| Message | `BTQ testnet genesis 22/Oct/2026` |
| nTime | `1792627260` (2026-10-22 00:01:00 UTC) |
| Nonce | `452194` |
| Hash | `0000022f03b97f577d6dcd5b776f93bb69535c3c84f3e1d4d66c4fa81f45a133` |
| Merkle root | `190d305f7e3f0e4d3b1a5a7d8a99b57dc05b107b54feadebfeda6320f7521d6d` |
| Magic | `71746e74` |
| P2P / RPC / onion | 19333 / 18332 / 18334 |
| HRP | `tbtq`, Dilithium `tdbt` |
| LWMA | height 1, window 144 |
| Dilithium and P2MR | height 1 |
| Min difficulty | off |
| DNS seeds | none yet |

`71746e74` is not `0c120a08`. `vSeeds` is empty so this binary does not dial `testnet-seed1.bitcoinquantum.com` or `testnet-seed2.bitcoinquantum.com`.
