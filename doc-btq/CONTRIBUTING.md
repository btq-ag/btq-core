# Contributing to BTQ

## Philosophy

BTQ follows Bitcoin Core's development philosophy: **review is more important than writing code**. We prioritize safety, correctness, and deliberate progress over speed. Every change is thoroughly reviewed, tested, and documented before integration.

### Core Principles

- **Security First**: Post-quantum cryptography requires exceptional care
- **Consensus Critical**: Changes affecting consensus rules receive extra scrutiny
- **Backward Compatibility**: Maintain compatibility where possible
- **Transparent Development**: All technical decisions happen in public on GitHub

## How to Propose Changes

### Before You Start

1. **Search existing issues** to avoid duplicates
2. **Discuss major changes** in GitHub Discussions first
3. **Start small** - prefer incremental changes over large rewrites
4. **Read the codebase** - understand existing patterns and conventions

### Types of Contributions

- **Bug Fixes**: Address security vulnerabilities, crashes, or incorrect behavior
- **Features**: Add new functionality (must align with roadmap)
- **Tests**: Improve test coverage or add new test scenarios
- **Documentation**: Clarify usage, improve developer experience
- **Performance**: Optimize critical paths with benchmarks

## Branching Model

### Branch Types

- **`master`**: Integration branch, always buildable
- **`release/X.Y`**: Cut at feature freeze, receives only blockers and backports
- **`feature/description`**: Development branches for new features
- **`fix/description`**: Bug fix branches

### Workflow

1. Fork the repository
2. Create a branch from `master`: `git checkout -b feature/your-feature`
3. Make your changes with tests
4. Submit a Pull Request against `master`

## Commit Standards

### Commit Message Format

```
area: short description (50 chars max)

Longer description explaining the change, motivation,
and any important implementation details.

- Use bullet points for multiple changes
- Reference issues: Fixes #123, Closes #456
- Include breaking changes and migration notes
```

### Examples

```bash
# Good commit messages
consensus: increase MAX_BLOCK_SERIALIZED_SIZE to 64 MiB
rpc: add getpqkeyinfo for post-quantum key inspection
tests: add functional test for Dilithium signature validation

# Areas to use
consensus, policy, mempool, p2p, rpc, wallet, script, crypto, build, tests, docs, gui
```

### Commit Guidelines

- **One logical change per commit**
- **Squash fixups** before final submission
- **Sign commits** if you have GPG configured
- **Keep history clean** - use interactive rebase when needed

## PR Workflow

### 1. Preparation

- [ ] Branch from latest `master`
- [ ] Include comprehensive tests
- [ ] Update documentation if needed
- [ ] Verify CI passes locally

### 2. Submission

Use the [Pull Request Template](.github/PULL_REQUEST_TEMPLATE.md) with:

- **Clear motivation** and problem statement
- **Implementation approach** and alternatives considered
- **Testing strategy** and instructions
- **Risk assessment** and compatibility notes
- **Release notes fragment**

### 3. Review Process

PRs go through multiple review stages:

1. **Concept ACK**: Reviewers agree with the general idea
2. **Approach ACK**: Reviewers approve the implementation approach
3. **Code Review**: Line-by-line review of implementation
4. **Testing**: Reviewers build and test the changes
5. **Final ACK**: Ready for merge (utACK or Tested ACK)

### 4. Merge Requirements

- [ ] **≥2 ACKs** from qualified reviewers
- [ ] **CI green** across all platforms
- [ ] **No unresolved NACKs**
- [ ] **Maintainer approval** for consensus/security changes
- [ ] **Squashed and rebased** for clean history

## Testing Expectations

### Required Tests

All code changes must include appropriate tests:

- **Unit Tests**: `src/test/` for core logic
- **Functional Tests**: `test/functional/` for RPC and integration
- **Fuzz Tests**: `src/test/fuzz/` for input parsing and validation

### Test Coverage Requirements

- **New code**: 100% line coverage where feasible
- **Modified code**: Maintain or improve existing coverage
- **Bug fixes**: Add regression tests
- **RPC changes**: Update functional tests

### Running Tests

```bash
# Unit tests
make check

# Functional tests
test/functional/test_runner.py

# Specific test
test/functional/test_runner.py wallet_basic.py

# Fuzz tests
src/test/fuzz/fuzz address_manager

# Lint checks
test/lint/lint-all.sh
```

## Release Notes Fragments

When making user-visible changes, include a release notes fragment:

```markdown
### RPC
- `getpqkeyinfo` RPC added to inspect post-quantum key information

### Wallet
- Added support for importing Dilithium descriptors

### Build
- Minimum required CMake version increased to 3.16
```

## Code Style

### C++

- Follow existing code style (similar to Bitcoin Core)
- Use `clang-format` for formatting
- Prefer explicit types over `auto` for readability
- Use RAII and smart pointers appropriately

### Python (Tests)

- Follow PEP 8
- Use type hints where helpful
- Keep test functions focused and well-named
- Add docstrings for complex test logic

### Documentation

- Use clear, concise language
- Include code examples where helpful
- Keep documentation up-to-date with code changes
- Use proper Markdown formatting

## Sensitive Code Guidelines

### Post-Quantum Cryptography

Changes to cryptographic code require special attention:

- **Domain Expert Review**: At least one cryptography expert must ACK
- **Constant-Time Operations**: Ensure timing-safe implementations
- **Test Vectors**: Include NIST KAT vectors where applicable
- **Fuzz Testing**: Add comprehensive fuzz targets for new parsers

### Consensus Changes

Any change affecting consensus rules:

- **Activation Guard**: Must be activation-height or version-bit gated
- **Backward Compatibility**: Document upgrade/downgrade behavior
- **Network Testing**: Test on signet/testnet before mainnet consideration
- **BIP Specification**: Consider writing a BIP for significant changes

## Getting Help

- **GitHub Discussions**: Design questions and roadmap discussions
- **GitHub Issues**: Bug reports and specific technical questions
- **Code Comments**: Ask questions directly on PR lines
- **Developer Notes**: Check [DEVELOPER_NOTES.md](dev/DEVELOPER_NOTES.md) for implementation details

## Recognition

Contributors are recognized in:

- Release notes credits section
- Git commit history
- Special recognition for security researchers and major contributors

Thank you for contributing to BTQ's mission of quantum-resistant cryptocurrency!
