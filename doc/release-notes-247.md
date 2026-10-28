Wallet
------

- Change now follows the inputs (Quarks F2.4): when a transaction spends a
  quantum-safe input (P2MR or dilithium-legacy) and nothing pins the change
  type, the change goes to a fresh P2MR script instead of an ECDSA one.
  This applies whether the input was preselected or picked by automatic
  coin selection. An explicit `-changetype`, a `change_type` argument, or a
  custom change address still wins. Change too small to be viable on a P2MR
  script is added to the fee instead of being left on a classical script.
  While an encrypted wallet is locked it cannot derive a Dilithium change
  key, so funding calls keep classical change and log a warning; unlock the
  wallet or pass `change_type` to control this.

- New mutable wallet flag `quantum_only` (`setwalletflag quantum_only`):
  the wallet refuses to create any destination other than P2MR, including
  change, ECDSA types, and the deprecated dilithium-legacy form. The flag
  occupies a mandatory flag bit, so older software refuses to open a wallet
  that has it set. `getwalletinfo` reports it.
