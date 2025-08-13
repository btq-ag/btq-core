# Libraries

| Name                     | Description |
|--------------------------|-------------|
| *libbtq_cli*         | RPC client functionality used by *btq-cli* executable |
| *libbtq_common*      | Home for common functionality shared by different executables and libraries. Similar to *libbtq_util*, but higher-level (see [Dependencies](#dependencies)). |
| *libbtq_consensus*   | Stable, backwards-compatible consensus functionality used by *libbtq_node* and *libbtq_wallet* and also exposed as a [shared library](../shared-libraries.md). |
| *libbtqconsensus*    | Shared library build of static *libbtq_consensus* library |
| *libbtq_kernel*      | Consensus engine and support library used for validation by *libbtq_node* and also exposed as a [shared library](../shared-libraries.md). |
| *libbtqqt*           | GUI functionality used by *btq-qt* and *btq-gui* executables |
| *libbtq_ipc*         | IPC functionality used by *btq-node*, *btq-wallet*, *btq-gui* executables to communicate when [`--enable-multiprocess`](multiprocess.md) is used. |
| *libbtq_node*        | P2P and RPC server functionality used by *btqd* and *btq-qt* executables. |
| *libbtq_util*        | Home for common functionality shared by different executables and libraries. Similar to *libbtq_common*, but lower-level (see [Dependencies](#dependencies)). |
| *libbtq_wallet*      | Wallet functionality used by *btqd* and *btq-wallet* executables. |
| *libbtq_wallet_tool* | Lower-level wallet functionality used by *btq-wallet* executable. |
| *libbtq_zmq*         | [ZeroMQ](../zmq.md) functionality used by *btqd* and *btq-qt* executables. |

## Conventions

- Most libraries are internal libraries and have APIs which are completely unstable! There are few or no restrictions on backwards compatibility or rules about external dependencies. Exceptions are *libbtq_consensus* and *libbtq_kernel* which have external interfaces documented at [../shared-libraries.md](../shared-libraries.md).

- Generally each library should have a corresponding source directory and namespace. Source code organization is a work in progress, so it is true that some namespaces are applied inconsistently, and if you look at [`libbtq_*_SOURCES`](../../src/Makefile.am) lists you can see that many libraries pull in files from outside their source directory. But when working with libraries, it is good to follow a consistent pattern like:

  - *libbtq_node* code lives in `src/node/` in the `node::` namespace
  - *libbtq_wallet* code lives in `src/wallet/` in the `wallet::` namespace
  - *libbtq_ipc* code lives in `src/ipc/` in the `ipc::` namespace
  - *libbtq_util* code lives in `src/util/` in the `util::` namespace
  - *libbtq_consensus* code lives in `src/consensus/` in the `Consensus::` namespace

## Dependencies

- Libraries should minimize what other libraries they depend on, and only reference symbols following the arrows shown in the dependency graph below:

<table><tr><td>

```mermaid

%%{ init : { "flowchart" : { "curve" : "basis" }}}%%

graph TD;

btq-cli[btq-cli]-->libbtq_cli;

btqd[btqd]-->libbtq_node;
btqd[btqd]-->libbtq_wallet;

btq-qt[btq-qt]-->libbtq_node;
btq-qt[btq-qt]-->libbtqqt;
btq-qt[btq-qt]-->libbtq_wallet;

btq-wallet[btq-wallet]-->libbtq_wallet;
btq-wallet[btq-wallet]-->libbtq_wallet_tool;

libbtq_cli-->libbtq_util;
libbtq_cli-->libbtq_common;

libbtq_common-->libbtq_consensus;
libbtq_common-->libbtq_util;

libbtq_kernel-->libbtq_consensus;
libbtq_kernel-->libbtq_util;

libbtq_node-->libbtq_consensus;
libbtq_node-->libbtq_kernel;
libbtq_node-->libbtq_common;
libbtq_node-->libbtq_util;

libbtqqt-->libbtq_common;
libbtqqt-->libbtq_util;

libbtq_wallet-->libbtq_common;
libbtq_wallet-->libbtq_util;

libbtq_wallet_tool-->libbtq_wallet;
libbtq_wallet_tool-->libbtq_util;

classDef bold stroke-width:2px, font-weight:bold, font-size: smaller;
class btq-qt,btqd,btq-cli,btq-wallet bold
```
</td></tr><tr><td>

**Dependency graph**. Arrows show linker symbol dependencies. *Consensus* lib depends on nothing. *Util* lib is depended on by everything. *Kernel* lib depends only on consensus and util.

</td></tr></table>

- The graph shows what _linker symbols_ (functions and variables) from each library other libraries can call and reference directly, but it is not a call graph. For example, there is no arrow connecting *libbtq_wallet* and *libbtq_node* libraries, because these libraries are intended to be modular and not depend on each other's internal implementation details. But wallet code is still able to call node code indirectly through the `interfaces::Chain` abstract class in [`interfaces/chain.h`](../../src/interfaces/chain.h) and node code calls wallet code through the `interfaces::ChainClient` and `interfaces::Chain::Notifications` abstract classes in the same file. In general, defining abstract classes in [`src/interfaces/`](../../src/interfaces/) can be a convenient way of avoiding unwanted direct dependencies or circular dependencies between libraries.

- *libbtq_consensus* should be a standalone dependency that any library can depend on, and it should not depend on any other libraries itself.

- *libbtq_util* should also be a standalone dependency that any library can depend on, and it should not depend on other internal libraries.

- *libbtq_common* should serve a similar function as *libbtq_util* and be a place for miscellaneous code used by various daemon, GUI, and CLI applications and libraries to live. It should not depend on anything other than *libbtq_util* and *libbtq_consensus*. The boundary between _util_ and _common_ is a little fuzzy but historically _util_ has been used for more generic, lower-level things like parsing hex, and _common_ has been used for btq-specific, higher-level things like parsing base58. The difference between util and common is mostly important because *libbtq_kernel* is not supposed to depend on *libbtq_common*, only *libbtq_util*. In general, if it is ever unclear whether it is better to add code to *util* or *common*, it is probably better to add it to *common* unless it is very generically useful or useful particularly to include in the kernel.


- *libbtq_kernel* should only depend on *libbtq_util* and *libbtq_consensus*.

- The only thing that should depend on *libbtq_kernel* internally should be *libbtq_node*. GUI and wallet libraries *libbtqqt* and *libbtq_wallet* in particular should not depend on *libbtq_kernel* and the unneeded functionality it would pull in, like block validation. To the extent that GUI and wallet code need scripting and signing functionality, they should be get able it from *libbtq_consensus*, *libbtq_common*, and *libbtq_util*, instead of *libbtq_kernel*.

- GUI, node, and wallet code internal implementations should all be independent of each other, and the *libbtqqt*, *libbtq_node*, *libbtq_wallet* libraries should never reference each other's symbols. They should only call each other through [`src/interfaces/`](`../../src/interfaces/`) abstract interfaces.

## Work in progress

- Validation code is moving from *libbtq_node* to *libbtq_kernel* as part of [The libbtqkernel Project #24303](https://github.com/btq/btq/issues/24303)
- Source code organization is discussed in general in [Library source code organization #15732](https://github.com/btq/btq/issues/15732)
