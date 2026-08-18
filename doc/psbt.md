# PSBT Howto for BTQ Core

Since BTQ Core 0.17, an RPC interface exists for Partially Signed BTQ
Transactions (PSBTs, as specified in
[BIP 174](https://github.com/btq/bips/blob/master/bip-0174.mediawiki)).

This document describes the overall workflow for producing signed transactions
through the use of PSBT, and the specific RPC commands used in typical
scenarios.

## PSBT in general

PSBT is an interchange format for BTQ transactions that are not fully signed
yet, together with relevant metadata to help entities work towards signing it.
It is intended to simplify workflows where multiple parties need to cooperate to
produce a transaction. Examples include hardware wallets, multisig setups, and
[CoinJoin](https://btqtalk.org/?topic=279249) transactions.

### Overall workflow

Overall, the construction of a fully signed BTQ transaction goes through the
following steps:

- A **Creator** proposes a particular transaction to be created. They construct
  a PSBT that contains certain inputs and outputs, but no additional metadata.
- For each input, an **Updater** adds information about the UTXOs being spent by
  the transaction to the PSBT. They also add information about the scripts and
  public keys involved in each of the inputs (and possibly outputs) of the PSBT.
- **Signers** inspect the transaction and its metadata to decide whether they
  agree with the transaction. They can use amount information from the UTXOs
  to assess the values and fees involved. If they agree, they produce a
  partial signature for the inputs for which they have relevant key(s).
- A **Finalizer** is run for each input to convert the partial signatures and
  possibly script information into a final `scriptSig` and/or `scriptWitness`.
- An **Extractor** produces a valid BTQ transaction (in network format)
  from a PSBT for which all inputs are finalized.

Generally, each of the above (excluding Creator and Extractor) will simply
add more and more data to a particular PSBT, until all inputs are fully signed.
In a naive workflow, they all have to operate sequentially, passing the PSBT
from one to the next, until the Extractor can convert it to a real transaction.
In order to permit parallel operation, **Combiners** can be employed which merge
metadata from different PSBTs for the same unsigned transaction.

The names above in bold are the names of the roles defined in BIP174. They're
useful in understanding the underlying steps, but in practice, software and
hardware implementations will typically implement multiple roles simultaneously.

## PSBT in BTQ Core

### RPCs

- **`converttopsbt` (Creator)** is a utility RPC that converts an
  unsigned raw transaction to PSBT format. It ignores existing signatures.
- **`createpsbt` (Creator)** is a utility RPC that takes a list of inputs and
  outputs and converts them to a PSBT with no additional information. It is
  equivalent to calling `createrawtransaction` followed by `converttopsbt`.
- **`walletcreatefundedpsbt` (Creator, Updater)** is a wallet RPC that creates a
  PSBT with the specified inputs and outputs, adds additional inputs and change
  to it to balance it out, and adds relevant metadata. In particular, for inputs
  that the wallet knows about (counting towards its normal or watch-only
  balance), UTXO information will be added. For outputs and inputs with UTXO
  information present, key and script information will be added which the wallet
  knows about. It is equivalent to running `createrawtransaction`, followed by
  `fundrawtransaction`, and `converttopsbt`.
- **`walletprocesspsbt` (Updater, Signer, Finalizer)** is a wallet RPC that takes as
  input a PSBT, adds UTXO, key, and script data to inputs and outputs that miss
  it, and optionally signs inputs. Where possible it also finalizes the partial
  signatures.
- **`utxoupdatepsbt` (Updater)** is a node RPC that takes a PSBT and updates it
  to include information available from the UTXO set (works only for SegWit
  inputs).
- **`finalizepsbt` (Finalizer, Extractor)** is a utility RPC that finalizes any
  partial signatures, and if all inputs are finalized, converts the result to a
  fully signed transaction which can be broadcast with `sendrawtransaction`.
- **`combinepsbt` (Combiner)** is a utility RPC that implements a Combiner. It
  can be used at any point in the workflow to merge information added to
  different versions of the same PSBT. In particular it is useful to combine the
  output of multiple Updaters or Signers.
- **`joinpsbts`** (Creator) is a utility RPC that joins multiple PSBTs together,
  concatenating the inputs and outputs. This can be used to construct CoinJoin
  transactions.
- **`decodepsbt`** is a diagnostic utility RPC which will show all information in
  a PSBT in human-readable form, as well as compute its eventual fee if known.
- **`analyzepsbt`** is a utility RPC that examines a PSBT and reports the
  current status of its inputs, the next step in the workflow if known, and if
  possible, computes the fee of the resulting transaction and estimates the
  final weight and feerate.


### Workflows

#### Multisig with multiple BTQ Core instances

For a quick start see [Basic M-of-N multisig example using descriptor wallets and PSBTs](./descriptors.md#basic-multisig-example).
If you are using legacy wallets feel free to continue with the example provided here.

Alice, Bob, and Carol want to create a 2-of-3 multisig address. They're all using
BTQ Core. We assume their wallets only contain the multisig funds. In case
they also have a personal wallet, this can be accomplished through the
multiwallet feature - possibly resulting in a need to add `-rpcwallet=name` to
the command line in case `btq-cli` is used.

Setup:
- All three call `getnewaddress` to create a new address; call these addresses
  *Aalice*, *Abob*, and *Acarol*.
- All three call `getaddressinfo "X"`, with *X* their respective address, and
  remember the corresponding public keys. Call these public keys *Kalice*,
  *Kbob*, and *Kcarol*.
- All three now run `addmultisigaddress 2 ["Kalice","Kbob","Kcarol"]` to teach
  their wallet about the multisig script. Call the address produced by this
  command *Amulti*. They may be required to explicitly specify the same
  addresstype option each, to avoid constructing different versions due to
  differences in configuration.
- They also run `importaddress "Amulti" "" false` to make their wallets treat
  payments to *Amulti* as contributing to the watch-only balance.
- Others can verify the produced address by running
  `createmultisig 2 ["Kalice","Kbob","Kcarol"]`, and expecting *Amulti* as
  output. Again, it may be necessary to explicitly specify the addresstype
  in order to get a result that matches. This command won't enable them to
  initiate transactions later, however.
- They can now give out *Amulti* as address others can pay to.

Later, when *V* BTQ has been received on *Amulti*, and Bob and Carol want to
move the coins in their entirety to address *Asend*, with no change. Alice
does not need to be involved.
- One of them - let's assume Carol here - initiates the creation. She runs
  `walletcreatefundedpsbt [] {"Asend":V} 0 {"subtractFeeFromOutputs":[0], "includeWatching":true}`.
  We call the resulting PSBT *P*. *P* does not contain any signatures.
- Carol needs to sign the transaction herself. In order to do so, she runs
  `walletprocesspsbt "P"`, and gives the resulting PSBT *P2* to Bob.
- Bob inspects the PSBT using `decodepsbt "P2"` to determine if the transaction
  has indeed just the expected input, and an output to *Asend*, and the fee is
  reasonable. If he agrees, he calls `walletprocesspsbt "P2"` to sign. The
  resulting PSBT *P3* contains both Carol's and Bob's signature.
- Now anyone can call `finalizepsbt "P3"` to extract a fully signed transaction
  *T*.
- Finally anyone can broadcast the transaction using `sendrawtransaction "T"`.

In case there are more signers, it may be advantageous to let them all sign in
parallel, rather than passing the PSBT from one signer to the next one. In the
above example this would translate to Carol handing a copy of *P* to each signer
separately. They can then all invoke `walletprocesspsbt "P"`, and end up with
their individually-signed PSBT structures. They then all send those back to
Carol (or anyone) who can combine them using `combinepsbt`. The last two steps
(`finalizepsbt` and `sendrawtransaction`) remain unchanged.

## Dilithium P2MR (BTQ)

ECDSA `addmultisigaddress` cannot describe a Dilithium spend. Dilithium
multisig lives in a P2MR (witness v2) leaf, and the partial signatures are too
large for `PSBT_IN_PARTIAL_SIG`. BTQ-PSBT adds typed input fields for the leaf,
the merkle root and the Dilithium signatures; `walletcreatefundedpsbt` /
`walletprocesspsbt` / `combinepsbt` / `finalizepsbt` are otherwise the same
roles as above.

A 2-of-3 is about 12 KB binary, 16 KB base64 — a file, not a QR code.
`analyzepsbt` does not yet report Dilithium progress; use `decodepsbt` (or the
wrapper's `status` command) and look at `inputs[0].p2mr_dilithium`.

### Raw `btq-cli` recipe (2-of-3)

Each co-signer has their own wallet. Replace `-rpcwallet=` and the `-datadir` /
`-testnet` flags to match the node.

```sh
# 1. Each co-signer publishes one Dilithium pubkey
btq-cli -rpcwallet=alice getnewdilithiumaddress
# { "address": "...", "p2mr_id": "..." }
btq-cli -rpcwallet=alice getdilithiumpubkey "<alice_p2mr_id>"
# { "pubkeys": [ { "pubkey": "<hex>", "ismine": true } ] }
# Repeat for bob and carol. Call the three pubkeys Kalice, Kbob, Kcarol.

# 2. Every co-signer registers the same m and the same pubkey list, in the same order
btq-cli -rpcwallet=alice createdilithiummultisig 2 '["Kalice","Kbob","Kcarol"]'
btq-cli -rpcwallet=bob   createdilithiummultisig 2 '["Kalice","Kbob","Kcarol"]'
btq-cli -rpcwallet=carol createdilithiummultisig 2 '["Kalice","Kbob","Kcarol"]'
# All three must report the same "address". Each reports "signers_available": 1.

# 3. Fund that address, then one co-signer builds the unsigned PSBT
btq-cli -rpcwallet=alice walletcreatefundedpsbt \
  '[{"txid":"<txid>","vout":0}]' '{"<destination>":0.0005}'
# Save the "psbt" field to unsigned.psbt (it is ~16 KB).

# 4a. Sequential: alice signs, hands the file to bob
btq-cli -rpcwallet=alice walletprocesspsbt "$(cat unsigned.psbt)"
btq-cli -rpcwallet=bob   walletprocesspsbt "<psbt from alice>"
# bob's result has "complete": true once two signatures are present.

# 4b. Parallel: alice and carol each sign the *unsigned* PSBT, then merge
btq-cli -rpcwallet=alice walletprocesspsbt "$(cat unsigned.psbt)"   # -> alice.psbt
btq-cli -rpcwallet=carol walletprocesspsbt "$(cat unsigned.psbt)"   # -> carol.psbt
btq-cli combinepsbt '["<alice.psbt>","<carol.psbt>"]'

# 5. Inspect, extract, broadcast
btq-cli decodepsbt "<psbt>"   # inputs[0].p2mr_dilithium.status, .signatures, .required
btq-cli finalizepsbt "<psbt>"
btq-cli sendrawtransaction "<hex>"
```

`createdilithiummultisig` is what makes the leaf and control block travel with
the PSBT: without it, `walletcreatefundedpsbt` has no P2MR metadata to attach
and the next wallet cannot sign.

### Wrapper

`contrib/btq/dilithium-psbt.sh` is the same recipe with less JSON quoting.
PSBT payloads stay on stdout so they can be redirected to a file:

```sh
CLI="btq-cli -testnet"
contrib/btq/dilithium-psbt.sh --cli="$CLI" pubkey alice
contrib/btq/dilithium-psbt.sh --cli="$CLI" register alice 2 "$PK_A" "$PK_B" "$PK_C"
contrib/btq/dilithium-psbt.sh --cli="$CLI" create alice "$TXID:0" "$DEST" 0.0005 > unsigned.psbt
contrib/btq/dilithium-psbt.sh --cli="$CLI" sign alice unsigned.psbt > alice.psbt
contrib/btq/dilithium-psbt.sh --cli="$CLI" sign carol unsigned.psbt > carol.psbt
contrib/btq/dilithium-psbt.sh --cli="$CLI" combine alice.psbt carol.psbt > combined.psbt
contrib/btq/dilithium-psbt.sh --cli="$CLI" status combined.psbt
contrib/btq/dilithium-psbt.sh --cli="$CLI" finalize combined.psbt > spend.hex
contrib/btq/dilithium-psbt.sh --cli="$CLI" broadcast spend.hex
```

The live-chain proof against public testnet is
`contrib/btq/testnet_dilithium_psbt_proof.sh`. The wire format and the two
confirmed spends are recorded in `doc-btq/DILITHIUM_PSBT_DESIGN.md` §16–§17.
