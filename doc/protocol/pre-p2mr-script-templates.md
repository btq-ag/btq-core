# Pre-P2MR Dilithium script templates

The public testnet contains Dilithium outputs of types that the P2MR-only rule withdrew. Any
node that syncs that chain has to validate them, so they are recorded here.

Under the **pre-P2MR** regime, Dilithium opcodes were valid in legacy scripts, P2SH, and
witness-v0 scripts, and a Dilithium-sized public key on a witness-v0 keyhash program routed to the
Dilithium verifier. That regime remains in force on the public testnet.

Under the **P2MR-only** regime — the mainnet parameter set, from genesis — Dilithium opcodes are
consensus-valid only inside P2MR tapscript leaves. None of the output types below are spendable
there.

## P2DPK (Pay-to-Dilithium-PubKey)

```
<1312-byte Dilithium2 pubkey> OP_CHECKSIGDILITHIUM
```

The quantum analogue of P2PK. The public key is committed on-chain at output creation, so the
unlocking data need only supply a valid signature.

Not suitable for long-held funds: the public key is visible in the UTXO set from the moment the
output is created, which meets the Weak Address definition under the Aggarwal–Babbush
classification. A sufficiently capable quantum adversary can derive the private key before the
output is spent.

The same script survives as a **P2MR leaf** — a single-key leaf committing one Dilithium public
key — where the Merkle root hides it until the spend and the exposure does not arise. Single-key
P2MR, not bare P2DPK, is the recommended coinbase payout target.

## Dilithium public-key-hash

```
OP_DUP OP_HASH160 <20-byte HASH160(Dilithium2 pubkey)> OP_EQUALVERIFY
OP_CHECKSIGDILITHIUM
```

The unlocking data carries the signature and the full public key. Same address-hash protection as
the P2PKH family: the output exposes only a hash before it is spent, and the public key appears
when the spend is broadcast.

Used directly or wrapped behind a script hash. The corresponding Base58 Dilithium address format
stays decodable for reading testnet history and is no longer generated.

## Witness-script Dilithium spends

Witness-v0 script-hash outputs can commit to a witness script containing `OP_CHECKSIGDILITHIUM` or
`OP_CHECKMULTISIGDILITHIUM`. At spend time the witness stack supplies the Dilithium signature
material and the witness script; the opcode in that script selects the Dilithium verifier.

This is the witness-script route by which Dilithium reached the interpreter before P2MR. Under the
P2MR-only rule the same multi-key policies are expressed as P2MR leaf scripts, either with
`OP_CHECKMULTISIGDILITHIUM` directly or as a threshold accumulator built from
`OP_CHECKSIGDILITHIUM` and `OP_ADD`.

## P2DWPKH (witness-v0 keyhash) — withdrawn

A 20-byte witness program committing to the HASH160 of a public key, with the spending key
supplied on the witness stack.

**The output script is identical for ECDSA and Dilithium.** It carries no opcode naming the
algorithm, so the interpreter selected the verifier from the size of the witness public key:

- A 33-byte compressed key took the ECDSA path, reconstructing
  `OP_DUP OP_HASH160 <program> OP_EQUALVERIFY OP_CHECKSIG`.
- A 1,312-byte Dilithium2 public key reconstructed the same script with `OP_CHECKSIGDILITHIUM`
  in place of `OP_CHECKSIG`.

This was the only place in the protocol where the spending algorithm was inferred rather than
named, and an output script that does not say which algorithm governs it is too easily confused
with the classical P2WPKH it is byte-identical to.

Under the P2MR-only rule the size heuristic is disabled: a Dilithium-sized key in a witness-v0
keyhash spend falls through to the ECDSA path and the spend is rejected. An output of this type
can still be paid to; it cannot be spent under Dilithium.

## Why the rule is exclusivity, not merely P2MR

BIP-360 specifies a Taproot-style output with no key-path spend, which is the mitigation Babbush
et al. §III.A recommend for Weak Address. Neither addresses whether a scheme's opcodes should be
confined to one script version. That confinement is a BTQ decision, on three grounds:

1. **Retroactivity.** Routing a witness-v0 keyhash spend to Dilithium by key size did not add an
   output type. It changed the spending rules of a witness program that already existed, so
   outputs created before the change became subject to rules they were not created under. A new
   witness version has no prior outputs whose semantics can change.
2. **No inference.** After activation, no consensus rule determines which signature scheme governs
   an output from the data supplied to spend it. In practice Dilithium and ECDSA witness programs
   proved easy to confuse, and one destination type gives wallets, miners, and explorers a single
   format to agree on.
3. **At-rest exposure.** A bare public-key output places the key on-chain at creation. A P2MR
   commitment withholds it until the spend.

## Provenance

The transaction field guide dated 2026-07-28, describing the public testnet at release
`v0.4.2-testnet`, records that the witness-v0 Dilithium format was withdrawn because Dilithium and
ECDSA witness programs proved too easy to confuse. That note is a snapshot of the format set
deployed at that release, not a specification.
