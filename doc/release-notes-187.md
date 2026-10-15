Default v2 transport
--------------------

BIP324 v2 transport is enabled by default. Connections configured with
`-addnode`, `-connect` or `-seednode` try v2 first and retain v1 fallback.
This also applies to previously configured peer lists. Use `-v2transport=0`
to retain v1-only operation.

The `addnode` RPC now follows the node's transport setting when its optional
`v2transport` argument is omitted. An explicit argument still selects the
requested transport, provided v2 is enabled locally.
