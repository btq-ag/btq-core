Shared Libraries
================

## btqconsensus

The purpose of this library is to make the verification functionality that is critical to BTQ's consensus available to other applications, e.g. to language bindings.

### API

The interface is defined in the C header `btqconsensus.h` located in `src/script/btqconsensus.h`.

#### Version

`btqconsensus_version` returns an `unsigned int` with the API version *(currently `3`)*. Version 3 is the first to accept the P2MR flags.

#### Script Validation

`btqconsensus_verify_script`, `btqconsensus_verify_script_with_amount` and `btqconsensus_verify_script_with_spent_outputs` return an `int` with the status of the verification. It will be `1` if the input script correctly spends the previous output `scriptPubKey`.

##### Parameters
###### btqconsensus_verify_script
- `const unsigned char *scriptPubKey` - The previous output script that encumbers spending.
- `unsigned int scriptPubKeyLen` - The number of bytes for the `scriptPubKey`.
- `const unsigned char *txTo` - The transaction with the input that is spending the previous output.
- `unsigned int txToLen` - The number of bytes for the `txTo`.
- `unsigned int nIn` - The index of the input in `txTo` that spends the `scriptPubKey`.
- `unsigned int flags` - The script validation flags *(see below)*.
- `btqconsensus_error* err` - Will have the error/success code for the operation *(see below)*.

###### btqconsensus_verify_script_with_amount
- `const unsigned char *scriptPubKey` - The previous output script that encumbers spending.
- `unsigned int scriptPubKeyLen` - The number of bytes for the `scriptPubKey`.
- `int64_t amount` - The amount spent in the input
- `const unsigned char *txTo` - The transaction with the input that is spending the previous output.
- `unsigned int txToLen` - The number of bytes for the `txTo`.
- `unsigned int nIn` - The index of the input in `txTo` that spends the `scriptPubKey`.
- `unsigned int flags` - The script validation flags *(see below)*.
- `btqconsensus_error* err` - Will have the error/success code for the operation *(see below)*.

###### btqconsensus_verify_script_with_spent_outputs
- `const unsigned char *scriptPubKey` - The previous output script that encumbers spending.
- `unsigned int scriptPubKeyLen` - The number of bytes for the `scriptPubKey`.
- `int64_t amount` - The amount spent in the input
- `const unsigned char *txTo` - The transaction with the input that is spending the previous output.
- `unsigned int txToLen` - The number of bytes for the `txTo`.
- `UTXO *spentOutputs` - Previous outputs spent in the transaction. `UTXO` is a struct composed by `const unsigned char *scriptPubKey`, `unsigned int scriptPubKeySize` (the number of bytes for the `scriptPubKey`) and `unsigned int value`.
- `unsigned int spentOutputsLen` - The number of bytes for the `spentOutputs`.
- `unsigned int nIn` - The index of the input in `txTo` that spends the `scriptPubKey`.
- `unsigned int flags` - The script validation flags *(see below)*.
- `btqconsensus_error* err` - Will have the error/success code for the operation *(see below)*.

##### Script Flags
- `btqconsensus_SCRIPT_FLAGS_VERIFY_NONE`
- `btqconsensus_SCRIPT_FLAGS_VERIFY_P2SH` - Evaluate P2SH ([BIP16](https://github.com/bitcoin/bips/blob/master/bip-0016.mediawiki)) subscripts
- `btqconsensus_SCRIPT_FLAGS_VERIFY_DERSIG` - Enforce strict DER ([BIP66](https://github.com/bitcoin/bips/blob/master/bip-0066.mediawiki)) compliance
- `btqconsensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY` - Enforce NULLDUMMY ([BIP147](https://github.com/bitcoin/bips/blob/master/bip-0147.mediawiki))
- `btqconsensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY` - Enable CHECKLOCKTIMEVERIFY ([BIP65](https://github.com/bitcoin/bips/blob/master/bip-0065.mediawiki))
- `btqconsensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY` - Enable CHECKSEQUENCEVERIFY ([BIP112](https://github.com/bitcoin/bips/blob/master/bip-0112.mediawiki))
- `btqconsensus_SCRIPT_FLAGS_VERIFY_WITNESS` - Enable WITNESS ([BIP141](https://github.com/bitcoin/bips/blob/master/bip-0141.mediawiki))
- `btqconsensus_SCRIPT_FLAGS_VERIFY_TAPROOT` - Enable TAPROOT ([BIP340](https://github.com/bitcoin/bips/blob/master/bip-0340.mediawiki), [BIP341](https://github.com/bitcoin/bips/blob/master/bip-0341.mediawiki), [BIP342](https://github.com/bitcoin/bips/blob/master/bip-0342.mediawiki))
- `btqconsensus_SCRIPT_FLAGS_VERIFY_DILITHIUM` - Enable Dilithium signature opcodes
- `btqconsensus_SCRIPT_FLAGS_VERIFY_P2MR` - Enable P2MR (witness v2 pay-to-merkle-root) script path spends
- `btqconsensus_SCRIPT_FLAGS_VERIFY_DILITHIUM_P2MR_ONLY` - Allow Dilithium opcodes only in P2MR tapscript
- `btqconsensus_SCRIPT_FLAGS_VERIFY_DILITHIUM_LIMITS` - Charge 1439 instead of 500 validation weight per P2MR Dilithium signature check, once `dilithium_limits` is active

##### Errors
- `btqconsensus_ERR_OK` - No errors with input parameters *(see the return value of `btqconsensus_verify_script` for the verification status)*
- `btqconsensus_ERR_TX_INDEX` - An invalid index for `txTo`
- `btqconsensus_ERR_TX_SIZE_MISMATCH` - `txToLen` did not match with the size of `txTo`
- `btqconsensus_ERR_DESERIALIZE` - An error deserializing `txTo`
- `btqconsensus_ERR_AMOUNT_REQUIRED` - Input amount is required if WITNESS is used
- `btqconsensus_ERR_INVALID_FLAGS` - Script verification `flags` are invalid (i.e. not part of the libconsensus interface)
- `btqconsensus_ERR_SPENT_OUTPUTS_REQUIRED` - Spent outputs are required if TAPROOT or P2MR is used
- `btqconsensus_ERR_SPENT_OUTPUTS_MISMATCH` - Spent outputs size doesn't match tx inputs size, or, when TAPROOT or P2MR is set, the spent output at `nIn` doesn't match `scriptPubKey` and `amount`

##### Matching the node
The node does not use `VERIFY_ALL`. To get the node's answer for a block at a given height, pass P2SH, DERSIG, NULLDUMMY, CHECKLOCKTIMEVERIFY, CHECKSEQUENCEVERIFY, WITNESS, TAPROOT and P2MR, plus:
- DILITHIUM once the `dilithium` deployment is active. Without it Dilithium spends fail.
- DILITHIUM_P2MR_ONLY once `dilithium_p2mr` is active. Without it the library still accepts Dilithium in legacy and witness v0 scripts, which the node rejects.
- DILITHIUM_LIMITS once `dilithium_limits` is active. Without it each P2MR Dilithium check is charged 500 validation weight instead of 1400.

Leaving out P2MR or WITNESS makes every witness v2 spend pass, like TAPROOT without WITNESS upstream. Callers that cleared TAPROOT to use `btqconsensus_verify_script_with_amount` must now clear P2MR too, or they get `btqconsensus_ERR_SPENT_OUTPUTS_REQUIRED`.

### Example Implementations
- [NBitcoin](https://github.com/MetacoSA/NBitcoin/blob/5e1055cd7c4186dee4227c344af8892aea54faec/NBitcoin/Script.cs#L979-#L1031) (.NET Bindings)
- [node-libbitcoinconsensus](https://github.com/bitpay/node-libbitcoinconsensus) (Node.js Bindings)
- [java-libbitcoinconsensus](https://github.com/dexX7/java-libbitcoinconsensus) (Java Bindings)
- [bitcoinconsensus-php](https://github.com/Bit-Wasp/bitcoinconsensus-php) (PHP Bindings)
- [rust-bitcoinconsensus](https://github.com/rust-bitcoin/rust-bitcoinconsensus) (Rust Bindings)