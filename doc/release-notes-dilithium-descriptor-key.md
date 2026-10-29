Dilithium keys in descriptor wallets
------------------------------------

Descriptor wallets derive Dilithium keys from the private key of the active
legacy descriptor. If that descriptor holds more than one private key, for
example an imported `sh(multi(1,xprvA/*,xprvB/*))`, the wallet now refuses to
derive new Dilithium keys instead of picking one of them.

This affects `getnewdilithiumaddress`, `getnewaddress` with a P2MR or
Dilithium type, and P2MR change, which the wallet uses when paying a P2MR or
Dilithium recipient. Those calls now return an error. Keys derived before the
upgrade are kept and still sign.

To fix it, import an active single-key descriptor such as `pkh(xprv/*)`. For
P2MR payments you can also pass `change_address` set to a P2MR address the
wallet already has. `-changetype` avoids the error too, but sends the change to
an ECDSA or Schnorr output.
