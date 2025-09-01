# BTQ Testing Guide

## Overview

BTQ maintains comprehensive test coverage across multiple layers to ensure reliability, security, and correctness. This guide covers all testing methodologies used in BTQ development.

## Test Types

### Unit Tests

**Location**: `src/test/`
**Purpose**: Test individual functions and classes in isolation
**Language**: C++
**Framework**: Boost.Test

**Coverage Areas**:
- Core data structures (CBlock, CTransaction, etc.)
- Cryptographic primitives
- Utility functions
- Consensus validation logic
- Network message parsing

### Functional Tests

**Location**: `test/functional/`
**Purpose**: End-to-end testing of BTQ daemon and CLI
**Language**: Python
**Framework**: Custom BTQ test framework

**Coverage Areas**:
- RPC interface testing
- P2P protocol behavior
- Wallet functionality
- Mining and consensus
- Configuration and startup

### Fuzz Tests

**Location**: `src/test/fuzz/`
**Purpose**: Find bugs through random input generation
**Language**: C++
**Framework**: libFuzzer

**Coverage Areas**:
- Input parsing and deserialization
- Network message handling
- Script execution
- Cryptographic operations
- Configuration parsing

### Lint Tests

**Location**: `test/lint/`
**Purpose**: Code style and quality enforcement
**Language**: Shell scripts and Python
**Framework**: Custom linting scripts

**Coverage Areas**:
- Code formatting
- Documentation standards
- Python code quality
- Shell script standards
- Commit message format

## Running Tests

### Unit Tests

**Run All Unit Tests:**
```bash
make check
```

**Run Specific Test Suite:**
```bash
src/test/test_btq --run_test=wallet_tests
```

**Run with Debugging:**
```bash
src/test/test_btq --run_test=crypto_tests --log_level=all
```

**Common Test Suites**:
- `crypto_tests`: Cryptographic function tests
- `script_tests`: Script validation and execution
- `transaction_tests`: Transaction validation
- `wallet_tests`: Wallet functionality
- `net_tests`: Network and P2P logic

### Functional Tests

**Run All Functional Tests:**
```bash
test/functional/test_runner.py
```

**Run Specific Test:**
```bash
test/functional/wallet_basic.py
```

**Run with Options:**
```bash
# Run in parallel
test/functional/test_runner.py -j 4

# Run extended test suite
test/functional/test_runner.py --extended

# Run with debugging
test/functional/wallet_basic.py --loglevel=DEBUG

# Run without cache
test/functional/test_runner.py --nocleanup
```

**BTQ-Specific Tests:**
```bash
# BTQ chain identity and mining
test/functional/btq_chain_identity.py
test/functional/btq_regtest_mining.py

# Post-quantum specific tests (when available)
test/functional/btq_dilithium_*.py
```

### Fuzz Tests

**Run Single Target:**
```bash
src/test/fuzz/fuzz address_manager
```

**Run with Specific Input:**
```bash
src/test/fuzz/fuzz transaction_deserialize < input_file
```

**Run with Time Limit:**
```bash
timeout 60 src/test/fuzz/fuzz script
```

**Generate Coverage Report:**
```bash
# Build with coverage
./configure --enable-fuzz --with-sanitizers=fuzzer
make
# Run fuzzer with coverage
src/test/fuzz/fuzz -print_coverage=1 target_name
```

### Lint Tests

**Run All Lint Checks:**
```bash
test/lint/lint-all.sh
```

**Run Specific Checks:**
```bash
test/lint/lint-python.py
test/lint/lint-shell.sh
test/lint/lint-format-strings.py
```

## Test Development

### Writing Unit Tests

**Basic Structure:**
```cpp
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(your_test_suite)

BOOST_AUTO_TEST_CASE(test_function_name)
{
    // Setup
    YourClass obj;
    
    // Execute
    bool result = obj.YourMethod(input);
    
    // Verify
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(obj.GetValue(), expected_value);
}

BOOST_AUTO_TEST_SUITE_END()
```

**Best Practices**:
- Test both success and failure cases
- Use descriptive test names
- Test edge cases and boundary conditions
- Mock external dependencies
- Keep tests fast and deterministic

### Writing Functional Tests

**Basic Structure:**
```python
#!/usr/bin/env python3
from test_framework.test_framework import BTQTestFramework
from test_framework.util import assert_equal

class YourTest(BTQTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
    
    def run_test(self):
        node = self.nodes[0]
        
        # Test your functionality
        result = node.your_rpc_call()
        assert_equal(result['field'], expected_value)

if __name__ == '__main__':
    YourTest().main()
```

**BTQ-Specific Considerations**:
```python
# Use RAW_P2PK for non-witness transactions
from test_framework.wallet import MiniWallet, MiniWalletMode
wallet = MiniWallet(node, mode=MiniWalletMode.RAW_P2PK)

# Use BTQ regtest chain explicitly
def set_test_params(self):
    self.chain = 'regtest'  # BTQ regtest
    
# Skip tests that require disabled features
def skip_test_if_missing_module(self):
    info = self.nodes[0].getblockchaininfo()
    if not info.get('softforks', {}).get('taproot', {}).get('active', False):
        self.skip_test_if_no_wallet()  # or raise SkipTest
```

### Writing Fuzz Tests

**Basic Structure:**
```cpp
#include <test/fuzz/fuzz.h>

FUZZ_TARGET(your_target_name)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    
    // Extract inputs from fuzzed data
    const std::vector<uint8_t> input = ConsumeRandomLengthByteVector(fuzzed_data_provider);
    
    // Test your function
    YourFunction(input);
    
    // No assertions needed - we're looking for crashes
}
```

**Post-Quantum Fuzz Targets**:
```cpp
// Example for Dilithium key parsing
FUZZ_TARGET(dilithium_key_parse)
{
    FuzzedDataProvider fdp(buffer.data(), buffer.size());
    const auto key_data = fdp.ConsumeRemainingBytes<uint8_t>();
    
    // Test key parsing doesn't crash
    DilithiumPublicKey key;
    key.SetFromBytes(key_data);  // Should handle invalid input gracefully
}
```

## Test Requirements for PRs

### Minimum Requirements

**All Code Changes:**
- [ ] Existing tests continue to pass
- [ ] New unit tests for new functions/methods
- [ ] Test coverage maintained or improved
- [ ] No flaky or non-deterministic tests introduced

**RPC Changes:**
- [ ] Functional test covering new RPC methods
- [ ] Test parameter validation and error cases
- [ ] Test help text accuracy
- [ ] Test backward compatibility

**Consensus Changes:**
- [ ] Comprehensive unit tests for validation logic
- [ ] Functional tests for activation behavior
- [ ] Test fork scenarios and edge cases
- [ ] Performance regression tests

**Cryptographic Changes:**
- [ ] Unit tests with known test vectors
- [ ] Fuzz targets for new parsers/serializers
- [ ] Constant-time operation verification
- [ ] Cross-implementation compatibility tests

### Quality Standards

**Test Reliability:**
- Tests must be deterministic
- No race conditions or timing dependencies
- Proper cleanup and resource management
- Clear failure messages and debugging info

**Test Performance:**
- Unit tests should complete in <1 second each
- Functional tests should complete in <60 seconds each
- Fuzz tests should find issues within reasonable time
- Test suite should scale well with parallel execution

## Platform-Specific Testing

### Linux Testing

**Required Distributions:**
- Ubuntu 20.04 LTS (minimum supported)
- Ubuntu 22.04 LTS (recommended)
- Debian 11 (stable)
- CentOS/RHEL 8+ (enterprise)

**Compiler Testing:**
- GCC 9+ (minimum required)
- Clang 10+ (recommended for development)
- Both debug and release builds

### macOS Testing

**Required Versions:**
- macOS 13 (minimum supported)
- macOS 14 (current)
- Both Intel and Apple Silicon

**Xcode Versions:**
- Latest stable Xcode
- Previous major version

### Windows Testing

**Required Versions:**
- Windows 10 (minimum supported)
- Windows 11 (current)

**Compiler:**
- Visual Studio 2019 (minimum)
- Visual Studio 2022 (recommended)

## Continuous Integration

### CI Matrix

BTQ runs comprehensive CI across multiple platforms and configurations:

**Linux Jobs:**
- Ubuntu 20.04 + GCC 9 (minimum requirements)
- Ubuntu 22.04 + GCC 11 (recommended)
- Ubuntu 22.04 + Clang 14 (alternative compiler)
- Ubuntu 22.04 + ASan/UBSan (memory safety)
- Ubuntu 22.04 + TSan (thread safety)

**macOS Jobs:**
- macOS 12 + Xcode 14 (Intel)
- macOS 13 + Xcode 15 (Apple Silicon)

**Windows Jobs:**
- Windows Server 2019 + VS2019
- Windows Server 2022 + VS2022

### CI Requirements

**All PRs Must:**
- Pass all CI jobs before merge consideration
- Include appropriate test coverage
- Pass lint checks and formatting
- Complete without memory leaks or undefined behavior

**Security-Sensitive PRs:**
- Must pass additional sanitizer jobs
- Require fuzz testing if applicable
- Need security officer review and approval

## Test Data and Fixtures

### Test Vectors

**Cryptographic Test Vectors:**
- Use NIST Known Answer Tests (KAT) where available
- Include test vectors from reference implementations
- Test both valid and invalid inputs
- Cover edge cases and boundary conditions

**Network Test Data:**
- Use deterministic test blockchain data
- Include various transaction types
- Test malformed message handling
- Cover protocol edge cases

### Test Environment Setup

**Regtest Configuration:**
```bash
# Standard regtest setup
btqd -regtest -daemon -datadir=./regtest_data

# With debugging
btqd -regtest -daemon -debug=all -datadir=./regtest_data

# With specific features
btqd -regtest -daemon -acceptdilithium=1 -datadir=./regtest_data
```

**Test Isolation:**
- Each test should use isolated data directories
- Clean up test data after completion
- No dependencies between test runs
- Deterministic test ordering

## Performance Testing

### Benchmarking

**Micro-Benchmarks:**
```bash
# Run specific benchmark
src/bench/bench_btq -filter=YourBenchmark

# Run all benchmarks
src/bench/bench_btq

# Output to file
src/bench/bench_btq > benchmark_results.txt
```

**Integration Benchmarks:**
- Full node sync performance
- Transaction validation throughput
- Memory usage under load
- Network bandwidth efficiency

### Performance Regression Testing

**Automated Checks:**
- CI includes performance regression detection
- Significant regressions block PR merge
- Performance improvements should be documented
- Benchmark results included in PR description

**Manual Performance Testing:**
- Large block processing
- High transaction volume scenarios
- Extended operation periods
- Resource constraint testing

## Debugging and Troubleshooting

### Common Issues

**Test Failures:**
- Check for timing issues in functional tests
- Verify test isolation and cleanup
- Check for platform-specific assumptions
- Review recent changes that might affect tests

**Build Issues:**
- Verify all dependencies are installed
- Check compiler version compatibility
- Review build configuration options
- Clean build directory and retry

**Fuzz Test Issues:**
- Increase timeout for slow targets
- Check for deterministic failures
- Review input validation logic
- Add debugging output if needed

### Debugging Tools

**GDB for Unit Tests:**
```bash
gdb --args src/test/test_btq --run_test=failing_test
```

**Valgrind for Memory Issues:**
```bash
valgrind --tool=memcheck src/test/test_btq
```

**Functional Test Debugging:**
```bash
# Keep test data for inspection
test/functional/your_test.py --nocleanup

# Enable verbose logging
test/functional/your_test.py --loglevel=DEBUG
```

## Test Coverage Analysis

### Coverage Tools

**Generate Coverage Report:**
```bash
# Configure with coverage
./configure --enable-lcov

# Build and run tests
make check

# Generate HTML report
make cov
```

**Coverage Targets:**
- Overall line coverage: >80%
- New code coverage: >95%
- Critical path coverage: 100%
- Error path coverage: >90%

### Coverage Review

**Required for PRs:**
- Coverage report for changed files
- Explanation for any uncovered lines
- Additional tests if coverage drops
- Focus on security-critical code paths

## Testing Best Practices

### Test Design

**Principles:**
- **Fast**: Unit tests complete quickly
- **Isolated**: No dependencies between tests
- **Deterministic**: Same input always produces same output
- **Readable**: Clear test names and structure
- **Maintainable**: Easy to update when code changes

**Anti-Patterns to Avoid:**
- Tests that depend on external services
- Tests with hardcoded timing assumptions
- Tests that modify global state
- Tests with unclear success/failure conditions

### BTQ-Specific Testing

**Post-Quantum Cryptography:**
- Test with NIST reference vectors
- Include negative tests for invalid keys/signatures
- Test serialization round-trips
- Verify constant-time operations

**Consensus Testing:**
- Test activation heights and version bits
- Include fork scenario testing
- Test large block handling
- Verify weight calculation changes

**Network Testing:**
- Test with BTQ-specific message types
- Verify port and protocol compatibility
- Test with mixed version networks
- Include stress testing scenarios

### Test Maintenance

**Regular Tasks:**
- Review and update test vectors
- Remove obsolete tests
- Improve test reliability
- Add tests for reported bugs

**Quarterly Reviews:**
- Analyze test coverage trends
- Identify undertested areas
- Plan test infrastructure improvements
- Review test performance and reliability

## Test Infrastructure

### Test Framework Features

**BTQ Test Framework Extensions:**
- MiniWallet with post-quantum support
- Descriptor-based transaction generation
- BTQ-specific chain and address handling
- Regtest mining utilities

**Utilities:**
- Address generation for BTQ networks
- Transaction creation helpers
- Block generation utilities
- Network simulation tools

### CI Integration

**Automated Testing:**
- All tests run on every PR
- Performance regression detection
- Coverage tracking and reporting
- Flaky test detection and quarantine

**Test Results:**
- Clear pass/fail reporting
- Detailed logs for failures
- Coverage reports
- Performance benchmark comparisons

## Troubleshooting Guide

### Common Test Failures

**Port Conflicts:**
```bash
# If tests fail with port binding errors
pkill -f btqd
test/functional/test_runner.py --portseed=12345
```

**Timing Issues:**
```bash
# For timing-sensitive tests
test/functional/your_test.py --timeout-factor=2
```

**Memory Issues:**
```bash
# Run with memory debugging
valgrind --tool=memcheck test/functional/your_test.py
```

### Platform-Specific Issues

**Linux:**
- Check for missing dependencies
- Verify kernel version compatibility
- Review file descriptor limits

**macOS:**
- Check Xcode command line tools
- Verify system integrity protection settings
- Review firewall configuration

**Windows:**
- Check Visual Studio installation
- Verify Windows SDK version
- Review antivirus interference

### Test Data Issues

**Cache Problems:**
```bash
# Clear test cache
rm -rf test/cache/
test/functional/test_runner.py
```

**Data Directory Issues:**
```bash
# Clean up test directories
rm -rf /tmp/btq_func_test_*
```

## Contributing Tests

### Test Contribution Guidelines

**When to Add Tests:**
- New functionality added
- Bug fixes implemented
- Edge cases discovered
- Security issues resolved

**Test Quality Standards:**
- Clear, descriptive test names
- Comprehensive coverage of functionality
- Proper error case testing
- Good documentation and comments

### Test Review Process

**Review Criteria:**
- Test correctness and completeness
- Performance and reliability
- Code quality and style
- Documentation and clarity

**Special Considerations:**
- Security-sensitive tests need expert review
- Performance tests need benchmark validation
- Fuzz tests need crash analysis
- Integration tests need end-to-end validation

---
