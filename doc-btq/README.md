# BTQ (Bitcoin Quantum)

## Overview

BTQ is a post-quantum POW cryptocurrency designed for the post-quantum era. It maintains Bitcoin's economic model and network architecture while implementing quantum-resistant cryptographic primitives and enhanced transaction capacity for large post-quantum signatures.

### Key Features

- **Post-Quantum Ready**: Integrated support for Dilithium, Falcon, and SPHINCS+ signature algorithms
- **Enhanced Capacity**: 64 MiB block size limit, 32 MiB soft cap, and normalized weight calculations
- **Bitcoin Compatibility**: Maintains Bitcoin's UTXO model, scripting system, and economic incentives
- **Quantum-Resistant Infrastructure**: PPK (Post-Quantum Key) infrastructure for future signature algorithm integration

### Roadmap Phases

- **Phase 1** (v0.1.0): Consensus parameter adjustments, block size increases, weight normalization
- **Phase 2** (v1.1.0): Dilithium integration into PPK infrastructure (no consensus activation)
- **Phase 3** (Future): Consensus activation of post-quantum signatures

## Getting Started

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/btq-ag/BTQ-Core.git
cd BTQ-Core

# Build dependencies (Ubuntu/Debian)
sudo apt-get install build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils

# Configure and build
./autogen.sh
./configure
make -j$(nproc)
```

### Running BTQ

#### Regtest (Development)
```bash
./src/btqd -regtest -daemon
./src/btq-cli -regtest getblockchaininfo
```

#### Testnet
```bash
./src/btqd -testnet -daemon
./src/btq-cli -testnet getblockchaininfo
```

#### Mainnet
```bash
./src/btqd -daemon
./src/btq-cli getblockchaininfo
```

### Network Information

| Network  | P2P   | RPC   | Onion  |
|----------|-------|-------|--------|
| Mainnet  | 9333  | 8332  | 8334   |
| Testnet  | 19333 | 18332 | 18334  |
| Signet   | 38333 | 38332 | 38334  |
| Regtest  | 19444 | 18443 | 18445  |

- **P2P**: Port for peer-to-peer node communication
- **RPC**: Port for JSON-RPC API access (btq-cli, applications)
- **Onion**: Target port for incoming Tor connections

## Contributing

BTQ follows Bitcoin Core's development model with some adaptations for post-quantum development:

- Read [CONTRIBUTING.md](CONTRIBUTING.md) for detailed contribution guidelines
- Follow the [Pull Request Template](.github/PULL_REQUEST_TEMPLATE.md)
- Review [Code Review Guidelines](reviews/CODE_REVIEW_GUIDELINES.md)
- Check [Testing Guide](testing/TESTING_GUIDE.md) for test requirements

### Quick Start for Contributors

1. Fork the repository
2. Create a feature branch from `master`
3. Make your changes with appropriate tests
4. Follow the PR template and submit for review
5. Address feedback and iterate through ACK process

## Documentation

- [Release Process](releases/RELEASE_PROCESS.md)
- [Security Policy](SECURITY.md)
- [Governance](GOVERNANCE.md)
- [Testing Guide](testing/TESTING_GUIDE.md)
- [Communication Policy](communication/COMMUNICATION.md)

## Support

- **GitHub Issues**: Bug reports and feature requests
- **GitHub Discussions**: Design discussions and Q&A
- **Telegram**: Community announcements and general discussion

## License

BTQ Core is released under the terms of the MIT license. See [COPYING](../COPYING) for more information.
