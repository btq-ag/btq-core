# Block capacity and witness scale

`WITNESS_SCALE_FACTOR` is 16 and `MAX_BLOCK_WEIGHT` is 8,000,000, both in
`src/consensus/consensus.h`. `MAX_BLOCK_SERIALIZED_SIZE` is 8,000,000 there too.
A non-witness byte costs 16 weight units. A witness byte costs 1. The
non-witness cap is the weight limit divided by the factor: 8,000,000 / 16 =
500,000 bytes per block. Witness data can run to about 8,000,000 bytes, and the
serialized block cannot exceed 8,000,000 bytes.

Both constants are consensus rules. Changing either after mainnet is a hard fork.

## Where 16 and 8,000,000 came from

v0.1.0 removed the witness discount. Weight equalled serialized size, and the
serialized block cap was 64 MiB
(`doc/release-notes/release-notes-btq-0.1.0.md`). Issue
[#216](https://github.com/btq-ag/btq-core/issues/216) records that a block could
be that large every minute. The factor of 16 came later, in PR #23.

[#19](https://github.com/btq-ag/btq-core/issues/19) (January 2026) said full
blocks at that size let the UTXO set grow by about 75 GB a day. Signatures can
be pruned. Outputs stay in the UTXO set, which every node keeps. It proposed a
scale factor of 32, 30-second blocks, and keeping the 4,000,000 weight limit
and a 4 MB block. That is 125,000 non-witness bytes per block. Barney
replied on 16 February that the team had implemented something very similar and
had cut UTXO bloat by about 250 times.

[PR #23](https://github.com/btq-ag/btq-core/pull/23) (merged 2026-02-11, commit
2b44ec5b7) set the scale factor to 16 and the block weight limit to 8,000,000.
That pair caps non-witness bytes at 500,000 per block. The PR stated three
reasons:

- Witness bytes cost a sixteenth of non-witness bytes, so a Dilithium signature
  (2,420 bytes) and public key (1,312 bytes) are cheapest in the witness, which
  nodes can prune.
- Non-witness data is what creates UTXOs. Capping it at 500,000 bytes per block
  brings maximum UTXO growth down to about 310 MB a day.
- A block still holds up to about 8 MB of witness data, about 20 transactions
  per second.

The same commit set the subsidy and the halving interval so 60-second blocks
keep Bitcoin's issuance curve, and set `MAX_STANDARD_TX_WEIGHT` to 400,000.
[#28](https://github.com/btq-ag/btq-core/issues/28) projected UTXO growth under
the weight values.

The weight limit depends on the factor. Non-witness bytes per block are
`MAX_BLOCK_WEIGHT / WITNESS_SCALE_FACTOR`. 8,000,000 with a factor of 16 is the
500,000-byte cap the PR stated. The same 8,000,000 with a factor of 32 would
allow 250,000 non-witness bytes. #19's 4,000,000 with a factor of 32 allows
125,000.

## 16 rather than 32

The reason for 16 rather than the 32 proposed in #19, or rather than 8, was not
recorded. The reason for 8,000,000 rather than 4,000,000 was not recorded
either.

| | #19 proposal | Adopted in #23 |
|---|---|---|
| Scale factor | 32 | 16 |
| Block interval | 30 s | 60 s |
| Block weight limit | 4,000,000 | 8,000,000 |
| Block size cap | 4 MB | 8 MB |
| Non-witness bytes per block | 125,000 | 500,000 |
| Non-witness bytes per 10 minutes | 2,500,000 | 5,000,000 |
| Total bytes per 10 minutes | about 80 MB | about 80 MB |

Bitcoin allows 1,000,000 non-witness bytes per 10 minutes. The two options
above allow about the same total data per 10 minutes. The adopted values allow
twice as much non-witness data as #19, so roughly twice #19's worst-case UTXO
growth.

#19's example used ML-DSA-87. The adopted example uses ML-DSA-44, which this
tree implements (1,312-byte public key, 2,420-byte signature). A smaller
signature may be part of why a lower factor was considered enough. Nothing
records that.

A single-leaf P2MR input spent with Dilithium, as worked in #216, has 41
non-witness bytes and a 3,746-byte witness. Weight is `41 * factor + 3746`.
Virtual size in that note:

| Factor | Weight | vB |
|---|---|---|
| 4 | 3,910 | 978 |
| 16 | 4,402 | 276 |
| 32 | 5,058 | 159 |

A higher factor makes that spend cheaper in vbytes. It also makes any other
witness data cheaper, and it shrinks the non-witness room in a standard
transaction unless `MAX_STANDARD_TX_WEIGHT` rises with the factor.

#19 also proposed taking Dilithium out of legacy scripts. The adopted rules
still allow it (`OP_CHECKSIGDILITHIUM`, 100,000-byte scripts). That point is
open in [#94](https://github.com/btq-ag/btq-core/issues/94).

## Standard transaction weight

`MAX_STANDARD_TX_WEIGHT` is 400,000 in `src/policy/policy.h`, the same number
Bitcoin uses. At a factor of 16 that is 25,000 non-witness bytes in a standard
transaction. Bitcoin allows 100,000. Whether leaving 400,000 in place was
deliberate was not recorded with the scale factor. It is a separate open
question in #94. This note does not decide it.

Two consequences already noted from that pairing, in #94 and
`doc/audit-briefing.md`: a legacy Dilithium spend puts its signature in the
scriptSig at 16 weight units per byte, which caps such a transaction at about
seven inputs. A P2WPKH input is 48 vB (768 weight units), against 68 vB (272)
on Bitcoin. A standard transaction holds about 520 of them rather than about
1,470.
