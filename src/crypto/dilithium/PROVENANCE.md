# Vendored Dilithium: provenance, specification, and known-answer tests

This directory holds the post-quantum signature implementation BTQ's consensus
rules depend on. This file records where it came from, which specification it
actually implements, how it differs from upstream, and how those claims are
tested. It exists because none of that was previously written down or checked.

## Which specification BTQ implements

**BTQ implements ML-DSA-44 as specified in FIPS 204**, under the pre-standard
CRYSTALS-Dilithium naming.

The naming is historical and pervasive — `Dilithium2`, `pqcrystals_dilithium2_ref`,
`OP_CHECKSIGDILITHIUM`, `BTQ_DILITHIUM_*` — but the algorithm is the standard.
The parameters in `ref/params.h` are post-round-3: `TRBYTES 64`, `RNDBYTES 32`,
and a `CTILDEBYTES` that varies 32/48/64 by security level. Round-3 Dilithium
has a 32-byte `tr`, no `rnd`, and a fixed 32-byte challenge hash. The signing
and verification entry points also take a context string, which is a FIPS 204
feature that round-3 Dilithium does not have.

This is not merely inferred from the parameters. The implementation is checked
directly against NIST's own ACVP vectors for ML-DSA-44 — key generation,
signature generation, and signature verification including negative cases — in
the `dilithium_kat_tests` unit suite. Those vectors pass.

Anything BTQ publishes about its cryptography should say ML-DSA-44 (FIPS 204),
noting the Dilithium naming as legacy. The two are the same thing here.

BTQ compiles only security level 2 (`DILITHIUM_MODE` defaults to `2` in
`ref/config.h`, and nothing overrides it). Levels 3 and 5 are present in the
vendored source and covered by the reference vectors, but are not built into
any BTQ binary.

## Upstream provenance

Upstream is <https://github.com/pq-crystals/dilithium>.

The vendored `ref/` tree corresponds to upstream commit:

```
d35ba3fe5449bee3e6d43e1f296c3ca818bd36be
```

Every file under `ref/` is byte-identical to that commit except `config.h`,
`sign.c`, `sign.h` and `api.h`. Those four carry exactly three deliberate
changes:

1. **Deterministic signing.** `config.h` leaves `DILITHIUM_RANDOMIZED_SIGNING`
   undefined. See the next section.

2. **Seeded key generation.** `sign.c` adds `crypto_sign_keypair_from_seed`,
   which takes a caller-supplied 32-byte seed where upstream calls
   `randombytes()`. The seed is expanded identically to upstream's random
   output, so a seeded keypair is an ordinary ML-DSA keypair. Upstream's
   `crypto_sign_keypair` is retained as a thin wrapper that draws a random seed
   and calls through, so the two paths cannot drift apart. BTQ's HD wallet
   derivation depends on this; the ACVP keyGen vectors are driven through it,
   which is what shows the seeded path produces standard keys.

3. **Constant-time challenge comparison.** `crypto_sign_verify_internal`
   compares the recomputed challenge with an accumulate-then-test loop instead
   of upstream's early-exit byte loop. This is a hardening change relative to
   upstream and does not affect which signatures verify.

No other file differs, including every file that performs arithmetic:
`poly.c`, `polyvec.c`, `packing.c`, `ntt.c`, `reduce.c`, `rounding.c` and
`fips202.c` are unmodified.

The one upstream change not reflected here is `d35ba3f` itself, a stricter
infinity-norm check added for upstream issue #113. It touches `avx2/sign.c`
only, and BTQ does not vendor that tree; our `ref/` already performs the check.

## The AVX2 tree was removed

Upstream also ships an AVX2-optimised implementation. BTQ vendored it at some
point but never in a usable state: 15 of its 43 files were zero bytes, including
`params.h`, so it had never compiled and could not. Nothing in the build
referenced it. It has been deleted.

Deleting it rather than repairing it was deliberate. AVX2 was evaluated and
declined on its merits — roughly a 2x gain on verification against no
performance problem, since worst-case block verification is 0.23 s, or 0.38% of
a 60-second block interval. A second implementation of a consensus-critical
primitive is a second thing that has to agree with the standard, and a place for
the two to disagree. Carrying a broken copy of one bought nothing and read, to
anyone reviewing the tree, as capability we had.

Note that `README.md` and the `Dilithium*_META.yml` files in this directory
still describe AVX2. Those are upstream's own files, kept verbatim as part of
the vendored snapshot; they document upstream's repository, not BTQ's build.

If AVX2 is ever wanted, the way back is to re-vendor it intact from upstream at
a pinned commit, wire it into the build, and hold it to the same two known-answer
tests described below — including `d35ba3f`, which the previously vendored copy
predated.

## Deterministic signing, not hedged

BTQ produces **deterministic** ML-DSA signatures: `rnd` is 32 zero bytes rather
than fresh randomness. FIPS 204 permits both; the hedged variant is the one
NIST recommends by default.

Upstream changed its own default to hedged in commit `ba7ae0b` (September 2024).
BTQ did not follow, and the evidence suggests that was accidental rather than
chosen: the `SHA256SUMS` manifest previously in this directory was taken from
`ba7ae0b`, the very commit that flipped the default, while the code kept the
deterministic setting. Since the manifest covers vectors generated in hedged
mode, it could never have matched this tree — which is why it appeared to
"verify nothing".

The choice is worth keeping deliberately rather than by inertia:

- Determinism is signer-side only. Verification does not depend on how a
  signature was produced, so this is **not a consensus rule** and can be changed
  later without a fork.
- Deterministic signatures make wallet behaviour reproducible and remove any
  dependence on RNG quality at signing time, which matters on the constrained
  and embedded signers a chain of this kind attracts.
- The trade-off is greater exposure to fault-injection and differential attacks
  against a physical signer, which the hedged variant is specifically designed
  to blunt.

`dilithium_kat_tests/signing_is_deterministic` fails if this is ever flipped, as
do the ACVP sigGen vectors, so the decision cannot change silently.

## The two known-answer tests

They answer different questions and are both needed.

### Supply chain: does our copy still behave like upstream?

`contrib/devtools/dilithium-kat.sh` compiles upstream's own `test_vectors`
harness against the sources in `ref/` and checks the output against
`SHA256SUMS` in this directory. It covers all three security levels over 10000
iterations each, exercising key generation, signing, verification, matrix
expansion, every pack/unpack routine, `decompose`, `power2round`, hint
generation and challenge sampling. It takes about a minute:

```sh
./contrib/devtools/dilithium-kat.sh
```

The digests in `SHA256SUMS` are upstream's own recorded values for the
deterministic configuration — the ones upstream published at `cf998be4`, the
last commit before hedged signing became the default. They are not
self-generated: building upstream `d35ba3f` with `DILITHIUM_RANDOMIZED_SIGNING`
disabled reproduces exactly these three digests, which is what establishes that
the local changes listed above are behaviour-preserving.

This check does not run under `make check`, because generating the vectors
takes far longer than the whole unit suite. Run it when changing anything under
`ref/`, and before a release.

### Conformance: does our implementation agree with the standard?

`src/test/dilithium_kat_tests.cpp` runs NIST ACVP vectors for ML-DSA-44 through
BTQ's own wrapper functions — `btq_dilithium_keypair_from_seed`,
`btq_dilithium_sign` and `btq_dilithium_verify` — rather than through the
reference API, so the test covers BTQ's code and not just upstream's. It runs
in the normal unit suite in well under a second:

```sh
./src/test/test_btq --run_test=dilithium_kat_tests
```

The vectors live in `src/test/data/mldsa44_acvp.json`, curated verbatim from
NIST's ACVP-Server repository at the revision recorded in that file's
`sourceRevision` field, so any value can be checked against NIST directly. The
subset covers the external/pure signature interface, which is the one BTQ calls
and which invokes ML-DSA's internal interface underneath.

The signature-verification vectors carry NIST's rejection reasons and include
cases that must be **refused** — modified message, modified commitment,
modified hint, modified `z`. Those matter more than the positive cases: they
pin down what the verifier must reject, which is the direction a consensus rule
gets attacked from.

## Why round-trip tests were not enough

`dilithium_key_tests` signs and then verifies, which catches gross breakage such
as a corrupted build or a broken serialisation path. It cannot catch a
self-consistent but subtly incorrect implementation, because such an
implementation passes a round-trip test perfectly. A divergence in a
rejection-sampling bound, a packing detail, or a hint-encoding edge case would
surface only when a BTQ signature met a verifier that was not our own binary —
or when someone hostile found it first. The vectors above are what close that
gap.
