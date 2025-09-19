# Dilithium Descriptor Testing Guide

This guide explains how to test the Dilithium descriptor implementation.

## 🧪 Testing Overview

The implementation includes multiple layers of testing:

1. **Unit Tests** - Test individual components
2. **Integration Tests** - Test wallet functionality
3. **Manual Tests** - Test with real BTQ Core
4. **RPC Tests** - Test via RPC interface

## 🚀 Quick Start

### 1. Quick Check
```bash
./test_dilithium_quick.sh
```
This runs basic checks to verify the implementation is present.

### 2. Compilation Test
```bash
make clean
make -j$(nproc)
```
Verify the code compiles without errors.

### 3. Unit Tests
```bash
make check
# Or specifically:
./src/test/test_btq --run_test=dilithium_descriptor_tests
```

## 📋 Detailed Testing

### Unit Tests (`src/test/dilithium_descriptor_tests.cpp`)

**What it tests:**
- Dilithium key generation
- Destination creation
- Address encoding
- Descriptor parsing
- Type compatibility
- Signature sizes

**Run with:**
```bash
./src/test/test_btq --run_test=dilithium_descriptor_tests
```

**Expected output:**
```
Running 7 test cases...
✓ dilithium_key_generation
✓ dilithium_destination_creation
✓ dilithium_address_encoding
✓ dilithium_descriptor_parsing
✓ dilithium_descriptor_expansion
✓ dilithium_type_compatibility
✓ dilithium_signature_sizes
All tests passed!
```

### Manual Testing (`test_dilithium_manual.sh`)

**What it tests:**
- Compilation with Dilithium support
- Descriptor parsing
- Wallet creation and management
- Address generation
- Descriptor wallet functionality
- Performance testing

**Run with:**
```bash
./test_dilithium_manual.sh
```

**Expected output:**
```
=== Dilithium Descriptor Manual Testing ===
1. Testing Compilation: ✓ Compilation successful
2. Running Unit Tests: ✓ Unit tests completed
3. Testing Descriptor Parsing: ✓ Descriptor strings created
4. Testing Wallet Integration: ✓ Generated Dilithium addresses
5. Testing Descriptor Wallet: ✓ Descriptor wallet created
6. Verifying Implementation: ✓ Dilithium functions found
7. Performance Testing: ✓ Performance test completed
=== Manual Testing Complete ===
```

### RPC Testing (`test_dilithium_rpc.py`)

**What it tests:**
- BTQ Core node startup
- Basic RPC functionality
- Wallet creation
- Dilithium address generation
- Descriptor wallet operations
- Error handling

**Run with:**
```bash
python3 test_dilithium_rpc.py
```

**Expected output:**
```
=== Dilithium Descriptor RPC Testing ===
Starting BTQ Core node...
✓ BTQ Core node started

=== Testing Basic Functionality ===
✓ Blockchain info: regtest chain
✓ Network info retrieved

=== Testing Wallet Creation ===
✓ Created wallet: dilithium_test_wallet
✓ Wallet info: dilithium_test_wallet

=== Testing Dilithium Address Generation ===
✓ Generated Dilithium legacy address: DilithiumLegacy_1234
  - Address valid: True
  - Is Dilithium: True
✓ Generated Dilithium bech32 address: dilithium1_5678
  - Address valid: True
  - Is Dilithium: True

=== Testing Descriptor Wallet ===
✓ Created descriptor wallet: dilithium_desc_wallet
✓ Imported Dilithium descriptor

=== Testing Error Handling ===
✓ Properly rejected invalid address type
✓ Properly rejected invalid descriptor

=== Test Results ===
Passed: 5/5
✓ All tests passed!
```

## 🔧 Manual Testing Commands

### 1. Start BTQ Core
```bash
./src/btqd -regtest -daemon
```

### 2. Create Wallet
```bash
./src/btq-cli -regtest createwallet "dilithium_test"
```

### 3. Generate Dilithium Addresses
```bash
# Legacy Dilithium address
./src/btq-cli -regtest getnewaddress "" "dilithium-legacy"

# Bech32 Dilithium address
./src/btq-cli -regtest getnewaddress "" "dilithium-bech32"
```

### 4. Test Descriptor Wallet
```bash
# Create descriptor wallet
./src/btq-cli -regtest createwallet "dilithium_desc" true

# Import Dilithium descriptor
./src/btq-cli -regtest importdescriptors '[{"desc":"pkh(xpub6BosfCnifzxcFwrSzQiqu2DBVTshkCXacvNsWGYJVVhhawA7d4R5WSWGFNbi8Aw6ZRc1brxMyWMzG3DSSSSoekkudhUd9yLb6qx39T9ECfY/1h/0h/0/*)","timestamp":"now"}]'
```

### 5. Validate Addresses
```bash
# Validate a Dilithium address
./src/btq-cli -regtest validateaddress "DilithiumLegacy_1234"
```

## 🐛 Debugging

### Common Issues

1. **Compilation Errors**
   ```bash
   make clean
   make -j$(nproc)
   ```

2. **Node Not Starting**
   ```bash
   # Check if port is in use
   netstat -tulpn | grep 18444
   
   # Kill existing processes
   pkill -f btqd
   ```

3. **RPC Connection Issues**
   ```bash
   # Check if node is running
   ./src/btq-cli -regtest getblockchaininfo
   ```

4. **Descriptor Import Errors**
   ```bash
   # Check descriptor syntax
   ./src/btq-cli -regtest help importdescriptors
   ```

### Debug Output

Enable debug logging:
```bash
./src/btqd -regtest -debug=wallet -debug=descriptor
```

Look for these debug messages:
- `DEBUG: DescriptorScriptPubKeyMan generating Dilithium address`
- `DEBUG: Generated Dilithium key successfully`
- `DEBUG: Created DilithiumPKHash destination`
- `DEBUG: Created DilithiumWitnessV0KeyHash destination`

## 📊 Performance Testing

### Key Generation Speed
```bash
time for i in {1..10}; do
    ./src/btq-cli -regtest getnewaddress "" "dilithium-legacy" >/dev/null
done
```

### Memory Usage
```bash
# Monitor memory usage during key generation
top -p $(pgrep btqd)
```

## ✅ Success Criteria

The implementation is working correctly if:

1. **Compilation**: No errors during `make`
2. **Unit Tests**: All tests pass
3. **Address Generation**: Can generate both legacy and bech32 Dilithium addresses
4. **Descriptor Support**: Can import and use Dilithium descriptors
5. **Error Handling**: Properly rejects invalid configurations
6. **Performance**: Key generation completes in reasonable time

## 📝 Test Results Template

```
=== Dilithium Descriptor Test Results ===
Date: [DATE]
Version: [BTQ_CORE_VERSION]

Compilation: ✓/✗
Unit Tests: ✓/✗ (X/Y passed)
Manual Tests: ✓/✗
RPC Tests: ✓/✗
Performance: ✓/✗ (X seconds for 10 keys)

Issues Found:
- [List any issues]

Notes:
- [Additional observations]
```

## 🚀 Next Steps

After successful testing:

1. **Integration**: Test with real transactions
2. **Performance**: Optimize key generation if needed
3. **Documentation**: Update user documentation
4. **Deployment**: Prepare for production use

## 📞 Support

If you encounter issues:

1. Check the debug output
2. Verify all dependencies are installed
3. Ensure BTQ Core is properly compiled
4. Review the implementation in `src/wallet/scriptpubkeyman.cpp`
