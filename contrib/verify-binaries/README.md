Verify binaries
===============

Release binaries are not signed today. BTQ does not publish `SHA256SUMS`,
`SHA256SUMS.asc`, or builder keys. There is no `security-keys.txt`.

`https://btqcore.org` and `https://btq.org` are not this project's. Do not
use them as sum hosts. `btqcore.org` was unregistered, so whoever registers
it can hand the script any `SHA256SUMS` they like. `btq.org` is someone
else's domain. Builder keys are not published. `github.com/btq-core/guix.sigs`
does not exist, and `bitcoin-core/guix.sigs` is not a BTQ key source.

`verify.py` has no default host. This exits with an error:

```sh
./contrib/verify-binaries/verify.py pub 22.0
```

The error is: BTQ does not publish signed binary sums there.

`--host` names servers you already mean to check. Repeat it to compare more
than one. The script still rejects `btqcore.org` and `btq.org`. Passing some
other `--host` does not mean BTQ signed what is on that server.
