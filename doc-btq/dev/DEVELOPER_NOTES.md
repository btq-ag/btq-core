# BTQ Developer Notes

## Repository Layout

### Source Code Organization

```
src/
├── consensus/          # Consensus validation logic
├── crypto/            # Cryptographic primitives
├── node/              # Node operation and management
├── net/               # Network and P2P protocol
├── policy/            # Transaction and block policy
├── primitives/        # Basic data structures
├── rpc/               # RPC server implementation
├── script/            # Script execution engine
├── util/              # Utility functions and helpers
├── wallet/            # Wallet functionality
├── zmq/               # ZeroMQ notification interface
├── init.cpp           # Initialization and startup
├── btqd.cpp          # Daemon main entry point
├── btq-cli.cpp       # CLI tool implementation
└── qt/                # GUI implementation (if applicable)
```

### Test Organization

```
test/
├── functional/        # End-to-end Python tests
├── fuzz/             # Fuzz testing targets
├── lint/             # Code style and quality checks
├── util/             # Test utilities and helpers
└── config.ini.in     # Test configuration template

src/test/             # Unit tests (C++)
src/wallet/test/      # Wallet-specific unit tests
```

### Build System

```
build-aux/            # Build system helpers
cmake/                # CMake configuration files
depends/              # Dependency management
contrib/              # Contributed tools and scripts
```

## Build Instructions

### Dependencies

#### Required Dependencies

**Linux (Ubuntu/Debian)**:
```bash
sudo apt-get update
sudo apt-get install build-essential libtool autotools-dev automake pkg-config
sudo apt-get install libssl-dev libevent-dev bsdmainutils python3
sudo apt-get install libboost-system-dev libboost-filesystem-dev libboost-test-dev
```

**macOS**:
```bash
# Install Xcode command line tools
xcode-select --install

# Install dependencies via Homebrew
brew install autoconf automake libtool pkg-config openssl libevent boost
```

**Windows**:
- Visual Studio 2019 or later
- vcpkg for dependency management
- See `build_msvc/README.md` for detailed instructions

#### Optional Dependencies

**Wallet Support**:
```bash
# BDB (legacy wallet)
sudo apt-get install libdb-dev libdb++-dev

# SQLite (descriptor wallet)
sudo apt-get install libsqlite3-dev
```

**ZeroMQ Support**:
```bash
sudo apt-get install libzmq3-dev
```

**GUI Support** (if applicable):
```bash
sudo apt-get install qtbase5-dev qttools5-dev-tools
```

### Build Process

#### Standard Build

```bash
# Generate build files
./autogen.sh

# Configure build
./configure

# Compile
make -j$(nproc)

# Install (optional)
sudo make install
```

#### Development Build

```bash
# Configure with debugging and all features
./configure --enable-debug --enable-wallet --with-gui=qt5 --enable-zmq

# Build with verbose output
make -j$(nproc) V=1
```

#### Cross-Compilation

**Windows (from Linux)**:
```bash
cd depends
make HOST=x86_64-w64-mingw32
cd ..
./autogen.sh
./configure --prefix=$PWD/depends/x86_64-w64-mingw32
make -j$(nproc)
```

**ARM64 (from x86_64)**:
```bash
cd depends  
make HOST=aarch64-linux-gnu
cd ..
./autogen.sh
./configure --prefix=$PWD/depends/aarch64-linux-gnu
make -j$(nproc)
```

### Configuration Options

#### Common Options

```bash
# Enable debugging symbols and assertions
--enable-debug

# Disable wallet functionality
--disable-wallet

# Enable ZeroMQ notifications
--enable-zmq

# Enable benchmark suite
--enable-bench

# Enable fuzz testing
--enable-fuzz

# Add sanitizers
--with-sanitizers=address,undefined
```

#### BTQ-Specific Options

```bash
# Enable post-quantum cryptography (when available)
--enable-dilithium
--enable-falcon
--enable-sphincs

# Configure signature algorithm defaults
--with-default-sig-algo=dilithium

# Enable quantum-resistant features
--enable-quantum-resistant
```

## Code Style Guidelines

### C++ Style

#### General Principles

- **Readability**: Code should be self-documenting
- **Consistency**: Follow existing patterns in the codebase
- **Safety**: Prefer RAII and smart pointers
- **Performance**: Avoid unnecessary allocations and copies

#### Naming Conventions

```cpp
// Classes: PascalCase
class TransactionValidator {
    // Public members: camelCase
    bool ValidateTransaction(const CTransaction& tx);
    
    // Private members: camelCase with m_ prefix
    bool m_isInitialized;
    
    // Constants: ALL_CAPS
    static const int MAX_TRANSACTION_SIZE = 1000000;
};

// Functions: camelCase
bool IsValidAddress(const std::string& address);

// Variables: camelCase
int blockHeight = 0;
std::string transactionId;

// Namespaces: lowercase
namespace consensus {
namespace validation {
    // ...
}
}
```

#### Code Formatting

**Indentation**: 4 spaces (no tabs)
**Line Length**: 120 characters maximum
**Braces**: Opening brace on same line
**Spacing**: Space after control flow keywords

```cpp
// Good formatting
if (condition) {
    DoSomething();
} else {
    DoSomethingElse();
}

// Function formatting
bool MyFunction(const std::string& param1,
                int param2,
                const CTransaction& param3)
{
    // Implementation
    return true;
}
```

#### Memory Management

```cpp
// Prefer smart pointers
std::unique_ptr<CTransaction> tx = std::make_unique<CTransaction>();

// Use RAII for resource management
{
    CAutoFile file(fopen("data.dat", "rb"), SER_DISK, CLIENT_VERSION);
    // File automatically closed when scope exits
}

// Avoid raw pointers for ownership
// Good
std::vector<std::unique_ptr<CNode>> nodes;

// Bad
std::vector<CNode*> nodes;  // Who owns these?
```

### Python Style (Tests)

#### General Guidelines

- Follow PEP 8 style guidelines
- Use type hints where helpful
- Keep functions focused and well-named
- Add docstrings for complex logic

```python
# Good Python style
def validate_transaction(tx_hex: str) -> bool:
    """Validate a transaction hex string.
    
    Args:
        tx_hex: Hexadecimal transaction string
        
    Returns:
        True if transaction is valid, False otherwise
        
    Raises:
        ValueError: If tx_hex is not valid hexadecimal
    """
    try:
        tx_bytes = bytes.fromhex(tx_hex)
        return len(tx_bytes) > 0
    except ValueError:
        raise ValueError(f"Invalid hex string: {tx_hex}")
```

#### Test-Specific Conventions

```python
# Test class naming
class WalletBackupTest(BTQTestFramework):
    
    # Test method naming
    def test_backup_and_restore(self):
        """Test wallet backup and restore functionality."""
        
    # Helper method naming  
    def _create_test_wallet(self):
        """Create a wallet for testing purposes."""
```

## Common Development Tasks

### Adding New RPC Methods

1. **Define RPC Handler** (`src/rpc/`):
```cpp
static RPCHelpMan your_new_rpc()
{
    return RPCHelpMan{"your_new_rpc",
        "Brief description of what this RPC does.\n",
        {
            {"param1", RPCArg::Type::STR, RPCArg::Optional::NO, "Description of param1"},
            {"param2", RPCArg::Type::NUM, RPCArg::Optional::YES, "Description of param2"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "result_field", "Description of result"},
            }
        },
        RPCExamples{
            HelpExampleCli("your_new_rpc", "\"param1_value\"")
            + HelpExampleRpc("your_new_rpc", "\"param1_value\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            // Implementation here
            UniValue result(UniValue::VOBJ);
            result.pushKV("result_field", "value");
            return result;
        }
    };
}
```

2. **Register RPC** (in appropriate `src/rpc/*.cpp` file):
```cpp
static const CRPCCommand commands[] = {
    // ... existing commands
    {"category", &your_new_rpc},
};
```

3. **Add Tests**:
```python
# In appropriate functional test
def test_your_new_rpc(self):
    result = self.nodes[0].your_new_rpc("param1_value")
    assert_equal(result['result_field'], "expected_value")
```

### Adding New Configuration Options

1. **Define Option** (`src/common/args.cpp`):
```cpp
argsman.AddArg("-yournewOption=<value>", 
               "Description of what this option does (default: default_value)", 
               ArgsManager::ALLOW_ANY, 
               OptionsCategory::OPTIONS);
```

2. **Use Option** (in relevant code):
```cpp
bool your_option_enabled = gArgs.GetBoolArg("-yournewOption", false);
```

3. **Add Documentation** (`doc/`):
- Update relevant documentation files
- Add to configuration examples
- Include in release notes

### Adding Post-Quantum Cryptography

1. **Key Type Definition** (`src/crypto/`):
```cpp
class DilithiumPublicKey {
public:
    static constexpr size_t SIZE = DILITHIUM_PUBLICKEY_BYTES;
    
    bool SetFromBytes(Span<const uint8_t> bytes);
    bool Verify(Span<const uint8_t> message, Span<const uint8_t> signature) const;
    std::vector<uint8_t> GetBytes() const;
};
```

2. **Descriptor Integration** (`src/script/descriptor.cpp`):
```cpp
// Add descriptor parsing for new key type
class DilithiumDescriptor : public DescriptorImpl {
    // Implementation
};
```

3. **Serialization Support** (`src/serialize.h`):
```cpp
template<typename Stream>
void Serialize(Stream& s, const DilithiumPublicKey& key) {
    auto bytes = key.GetBytes();
    s << bytes;
}
```

4. **Test Coverage**:
```cpp
// Unit tests
BOOST_AUTO_TEST_CASE(dilithium_key_operations)
{
    DilithiumPublicKey key;
    // Test key operations
}

// Fuzz tests
FUZZ_TARGET(dilithium_key_parse) {
    // Fuzz key parsing
}
```

## Debugging and Development Tools

### Debugging BTQ

#### GDB Configuration

**`.gdbinit` Setup**:
```gdb
# Pretty printers for BTQ types
python
import sys
sys.path.insert(0, '/path/to/btq/contrib/gdb')
import btq_gdb
end

# Useful aliases
alias bt-full = thread apply all bt full
alias print-tx = call PrintTransaction($arg0)
```

#### Common Debugging Scenarios

**Debugging Crashes**:
```bash
# Run with core dumps enabled
ulimit -c unlimited
gdb --args src/btqd -regtest -debug=all

# Analyze core dump
gdb src/btqd core.12345
(gdb) bt
(gdb) info registers
```

**Debugging Hangs**:
```bash
# Attach to running process
gdb -p $(pgrep btqd)
(gdb) thread apply all bt
(gdb) info threads
```

**Memory Debugging**:
```bash
# Run with Valgrind
valgrind --tool=memcheck --leak-check=full src/btqd -regtest

# Run with AddressSanitizer
./configure --with-sanitizers=address
make
src/btqd -regtest
```

### Performance Profiling

#### CPU Profiling

**Using perf (Linux)**:
```bash
# Profile BTQ daemon
perf record -g src/btqd -regtest
perf report

# Profile specific operations
perf record -g src/bench/bench_btq -filter=YourBenchmark
```

**Using Instruments (macOS)**:
```bash
# Profile with Xcode Instruments
instruments -t "Time Profiler" src/btqd -regtest
```

#### Memory Profiling

**Valgrind (Linux)**:
```bash
# Memory usage profiling
valgrind --tool=massif src/btqd -regtest
ms_print massif.out.12345
```

**Heap Profiling**:
```bash
# With gperftools
./configure LDFLAGS=-ltcmalloc
make
HEAPPROFILE=/tmp/btq.hprof src/btqd -regtest
```

### Static Analysis

#### Clang Static Analyzer

```bash
# Run static analysis
scan-build make

# View results
scan-view /tmp/scan-build-*
```

#### Cppcheck

```bash
# Run cppcheck on source
cppcheck --enable=all --inconclusive --xml src/ 2> cppcheck.xml

# Generate HTML report
cppcheck-htmlreport --file=cppcheck.xml --report-dir=cppcheck-report
```

## Common Tools

### Development Environment Setup

#### VS Code Configuration

**`.vscode/settings.json`**:
```json
{
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "C_Cpp.default.cppStandard": "c++17",
    "C_Cpp.default.includePath": [
        "${workspaceFolder}/src",
        "${workspaceFolder}/depends/x86_64-pc-linux-gnu/include"
    ],
    "python.defaultInterpreter": "/usr/bin/python3",
    "python.linting.enabled": true,
    "python.linting.flake8Enabled": true
}
```

**`.vscode/tasks.json`**:
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "build",
            "type": "shell",
            "command": "make",
            "args": ["-j", "4"],
            "group": "build",
            "presentation": {
                "echo": true,
                "reveal": "always",
                "focus": false,
                "panel": "shared"
            }
        },
        {
            "label": "test",
            "type": "shell", 
            "command": "make",
            "args": ["check"],
            "group": "test",
            "dependsOn": "build"
        }
    ]
}
```

#### CLion Configuration

**CMakeLists.txt Integration**:
- Use CMake build system for IDE integration
- Configure include paths and compile definitions
- Set up debugging configurations
- Enable static analysis integration

### Useful Scripts

#### Development Helpers

**Quick Build Script** (`scripts/dev-build.sh`):
```bash
#!/bin/bash
set -e

echo "Building BTQ for development..."
./autogen.sh
./configure --enable-debug --enable-wallet --enable-zmq
make -j$(nproc)
echo "Build complete!"
```

**Test Runner Script** (`scripts/run-tests.sh`):
```bash
#!/bin/bash
set -e

echo "Running unit tests..."
make check

echo "Running functional tests..."
test/functional/test_runner.py -j4

echo "Running lint checks..."
test/lint/lint-all.sh

echo "All tests passed!"
```

#### Code Quality Scripts

**Format Code** (`scripts/format-code.sh`):
```bash
#!/bin/bash
# Format all C++ files
find src -name "*.cpp" -o -name "*.h" | xargs clang-format -i

# Format Python files
find test -name "*.py" | xargs autopep8 -i
```

**Check Style** (`scripts/check-style.sh`):
```bash
#!/bin/bash
# Check C++ formatting
git diff --name-only HEAD~1 | grep -E '\.(cpp|h)$' | xargs clang-format --dry-run --Werror

# Check Python formatting  
git diff --name-only HEAD~1 | grep '\.py$' | xargs flake8
```

## Post-Quantum Development

### Cryptographic Integration

#### Key Type Implementation

```cpp
// Example key type structure
class DilithiumPublicKey {
private:
    std::array<uint8_t, DILITHIUM_PUBLICKEY_BYTES> m_data;
    bool m_valid;

public:
    DilithiumPublicKey() : m_valid(false) {}
    
    bool SetFromBytes(Span<const uint8_t> bytes);
    bool Verify(Span<const uint8_t> message, Span<const uint8_t> signature) const;
    std::vector<uint8_t> GetBytes() const;
    bool IsValid() const { return m_valid; }
};
```

#### Serialization Patterns

```cpp
// Serialization for new types
template<typename Stream>
void Serialize(Stream& s, const DilithiumPublicKey& key) {
    if (!key.IsValid()) {
        throw std::ios_base::failure("Cannot serialize invalid key");
    }
    auto bytes = key.GetBytes();
    WriteCompactSize(s, bytes.size());
    s.write(MakeByteSpan(bytes));
}

template<typename Stream>  
void Unserialize(Stream& s, DilithiumPublicKey& key) {
    auto size = ReadCompactSize(s);
    if (size != DilithiumPublicKey::SIZE) {
        throw std::ios_base::failure("Invalid key size");
    }
    std::vector<uint8_t> bytes(size);
    s.read(MakeWritableByteSpan(bytes));
    if (!key.SetFromBytes(bytes)) {
        throw std::ios_base::failure("Invalid key data");
    }
}
```

### Testing Post-Quantum Code

#### Unit Test Patterns

```cpp
BOOST_AUTO_TEST_CASE(dilithium_key_roundtrip)
{
    // Test serialization round-trip
    DilithiumKeyPair keypair;
    keypair.Generate();
    
    auto serialized = keypair.GetPublicKey().GetBytes();
    DilithiumPublicKey restored;
    BOOST_CHECK(restored.SetFromBytes(serialized));
    BOOST_CHECK(restored.GetBytes() == serialized);
}

BOOST_AUTO_TEST_CASE(dilithium_signature_validation)
{
    // Test signature validation
    DilithiumKeyPair keypair;
    keypair.Generate();
    
    std::vector<uint8_t> message = {0x01, 0x02, 0x03};
    auto signature = keypair.Sign(message);
    
    BOOST_CHECK(keypair.GetPublicKey().Verify(message, signature));
    
    // Test with modified message
    message[0] = 0xFF;
    BOOST_CHECK(!keypair.GetPublicKey().Verify(message, signature));
}
```

#### Functional Test Patterns

```python
def test_dilithium_rpc(self):
    """Test Dilithium key operations via RPC."""
    node = self.nodes[0]
    
    # Generate new Dilithium key
    key_info = node.generatedilithiumkey()
    assert 'public_key' in key_info
    assert 'address' in key_info
    
    # Import and verify
    imported = node.importdilithiumkey(key_info['private_key'])
    assert imported['success']
    
    # Test signing
    message = "test message"
    signature = node.signdilithium(message, key_info['address'])
    verified = node.verifydilithium(message, signature, key_info['public_key'])
    assert verified['valid']
```

## Performance Considerations

### Critical Paths

**Block Validation**:
- Transaction signature verification
- Script execution
- UTXO database operations
- Merkle tree calculations

**Network Operations**:
- Message serialization/deserialization
- Peer connection management
- Inventory processing
- Block relay

**Wallet Operations**:
- Key derivation and storage
- Transaction creation and signing
- Balance calculation
- Address generation

### Optimization Guidelines

#### Memory Optimization

```cpp
// Good: Reserve capacity for known sizes
std::vector<CTransaction> transactions;
transactions.reserve(block.vtx.size());

// Good: Use move semantics
return std::move(large_object);

// Avoid: Unnecessary copies
// Bad
std::vector<uint8_t> data = GetLargeData();  // Copy
ProcessData(data);  // Another copy

// Good  
const auto& data = GetLargeData();  // Reference
ProcessData(data);  // No copy
```

#### CPU Optimization

```cpp
// Good: Cache expensive calculations
class ExpensiveCalculator {
    mutable std::optional<Result> m_cached_result;
public:
    Result GetResult() const {
        if (!m_cached_result) {
            m_cached_result = DoExpensiveCalculation();
        }
        return *m_cached_result;
    }
};

// Good: Use efficient algorithms
// Use std::unordered_map for O(1) lookups instead of std::map O(log n)
std::unordered_map<uint256, CTransaction> tx_cache;
```

## Security Guidelines

### Cryptographic Code

#### Constant-Time Operations

```cpp
// Good: Constant-time comparison
bool SecureEqual(Span<const uint8_t> a, Span<const uint8_t> b) {
    if (a.size() != b.size()) return false;
    
    uint8_t result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= a[i] ^ b[i];
    }
    return result == 0;
}

// Bad: Variable-time comparison
bool InsecureEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a == b;  // May leak timing information
}
```

#### Key Material Handling

```cpp
// Good: Secure memory for sensitive data
class SecureString : public std::vector<char, secure_allocator<char>> {
public:
    ~SecureString() {
        memory_cleanse(data(), size());
    }
};

// Good: Clear sensitive data
void ProcessPrivateKey(const std::vector<uint8_t>& key) {
    // ... process key
    memory_cleanse(const_cast<uint8_t*>(key.data()), key.size());
}
```

### Input Validation

```cpp
// Good: Comprehensive validation
bool ValidateInput(const std::string& input) {
    if (input.empty() || input.size() > MAX_INPUT_SIZE) {
        return false;
    }
    
    // Check for valid characters
    for (char c : input) {
        if (!IsValidChar(c)) {
            return false;
        }
    }
    
    return true;
}

// Good: Safe parsing
bool ParseAmount(const std::string& str, CAmount& amount) {
    try {
        amount = std::stoll(str);
        return amount >= 0 && amount <= MAX_MONEY;
    } catch (const std::exception&) {
        return false;
    }
}
```

## Contributing Workflow

### Development Cycle

1. **Issue Creation**: Identify problem or enhancement
2. **Discussion**: Design discussion in GitHub
3. **Implementation**: Code changes with tests
4. **Review**: Peer review and iteration
5. **Testing**: Comprehensive testing
6. **Merge**: Integration into master branch
7. **Follow-up**: Monitor for issues and regressions

### Quality Checklist

**Before Submitting PR**:
- [ ] Code follows style guidelines
- [ ] All tests pass locally
- [ ] Documentation updated
- [ ] Performance impact assessed
- [ ] Security considerations reviewed

**During Review**:
- [ ] Address all reviewer feedback
- [ ] Update tests based on review
- [ ] Maintain clean commit history
- [ ] Keep PR focused and reasonably sized

**After Merge**:
- [ ] Monitor for regressions
- [ ] Update related documentation
- [ ] Consider backport needs
- [ ] Celebrate the contribution!

---
