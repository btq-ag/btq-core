# BTQ Continuous Integration Guide

## Overview

BTQ maintains a comprehensive CI pipeline that ensures code quality, security, and compatibility across all supported platforms. This guide covers the CI architecture, requirements, and maintenance procedures.

## CI Philosophy

### Core Principles

- **Quality Gates**: No code merges without passing all required checks
- **Platform Coverage**: Test on all supported operating systems and architectures
- **Security Focus**: Include security-oriented testing in every run
- **Performance Monitoring**: Track performance regressions automatically
- **Fast Feedback**: Provide quick feedback to developers while maintaining thoroughness

## CI Matrix

### Platform Coverage

#### Linux Jobs

**Ubuntu 20.04 (Minimum Supported)**
- **Compiler**: GCC 9
- **Purpose**: Verify minimum requirements
- **Tests**: Unit, functional, lint
- **Build Type**: Release
- **Special**: Minimum dependency versions

**Ubuntu 22.04 (Primary Development)**
- **Compiler**: GCC 11
- **Purpose**: Primary development platform
- **Tests**: Unit, functional, fuzz (short), lint
- **Build Type**: Debug + Release
- **Special**: Full test coverage analysis

**Ubuntu 22.04 (Alternative Compiler)**
- **Compiler**: Clang 14
- **Purpose**: Compiler compatibility
- **Tests**: Unit, functional
- **Build Type**: Release
- **Special**: Different compiler warnings and optimizations

#### macOS Jobs

**macOS 12 (Intel)**
- **Compiler**: Xcode 14
- **Purpose**: Intel Mac compatibility
- **Tests**: Unit, functional
- **Build Type**: Release
- **Special**: x86_64 architecture testing

**macOS 13 (Apple Silicon)**
- **Compiler**: Xcode 15
- **Purpose**: Apple Silicon compatibility
- **Tests**: Unit, functional
- **Build Type**: Release
- **Special**: ARM64 architecture testing

#### Windows Jobs

**Windows Server 2019**
- **Compiler**: Visual Studio 2019
- **Purpose**: Minimum Windows support
- **Tests**: Unit, functional (subset)
- **Build Type**: Release
- **Special**: Legacy Windows compatibility

**Windows Server 2022**
- **Compiler**: Visual Studio 2022
- **Purpose**: Current Windows support
- **Tests**: Unit, functional
- **Build Type**: Debug + Release
- **Special**: Latest Windows features

### Sanitizer Jobs

#### AddressSanitizer (ASan)

**Configuration**:
```bash
./configure --with-sanitizers=address --enable-debug
make -j$(nproc)
```

**Purpose**: Detect memory errors
- Buffer overflows and underflows
- Use-after-free bugs
- Memory leaks
- Stack and heap corruption

**Tests Run**:
- Full unit test suite
- Core functional tests
- Short fuzz runs

#### UndefinedBehaviorSanitizer (UBSan)

**Configuration**:
```bash
./configure --with-sanitizers=undefined --enable-debug
make -j$(nproc)
```

**Purpose**: Detect undefined behavior
- Integer overflow/underflow
- Null pointer dereferences
- Invalid enum values
- Misaligned memory access

#### ThreadSanitizer (TSan)

**Configuration**:
```bash
./configure --with-sanitizers=thread --enable-debug
make -j$(nproc)
```

**Purpose**: Detect race conditions
- Data races
- Deadlocks
- Thread safety violations
- Atomic operation issues

**Special Considerations**:
- Slower execution (2-20x overhead)
- May require test timeout adjustments
- Some false positives require suppression

#### MemorySanitizer (MSan)

**Configuration**:
```bash
./configure --with-sanitizers=memory --enable-debug
make -j$(nproc)
```

**Purpose**: Detect uninitialized memory reads
- Uninitialized variables
- Uninitialized struct members
- Information leaks through uninitialized data

## Specialized Jobs

### Fuzz Testing Job

**Configuration**:
```bash
./configure --enable-fuzz --with-sanitizers=fuzzer,address,undefined
make -j$(nproc)
```

**Execution**:
- Run each fuzz target for 5 minutes
- Collect and archive any crashes
- Update corpus with interesting inputs
- Generate coverage reports

**Targets Tested**:
- All network message parsers
- Cryptographic operation fuzzers
- Transaction and block parsing
- Post-quantum specific targets

### Performance Regression Job

**Benchmarking**:
```bash
# Build optimized version
./configure --enable-bench
make -j$(nproc)

# Run benchmarks
src/bench/bench_btq > benchmark_results.txt
```

**Regression Detection**:
- Compare against baseline performance
- Flag significant regressions (>5% slowdown)
- Track performance trends over time
- Generate performance reports

### Security Analysis Job

**Static Analysis**:
```bash
# Clang static analyzer
scan-build make

# Cppcheck
cppcheck --enable=all --inconclusive src/
```

**Dependency Scanning**:
- Check for known vulnerabilities in dependencies
- Verify dependency integrity and signatures
- Monitor for supply chain attacks
- Update dependency versions automatically

### Cross-Compilation Jobs

**ARM64 Cross-Compilation**:
```bash
./configure --host=aarch64-linux-gnu
make -j$(nproc)
```

**RISC-V Cross-Compilation** (future):
```bash
./configure --host=riscv64-linux-gnu  
make -j$(nproc)
```

## CI Requirements

### Mandatory Checks

**All PRs Must Pass:**
- [ ] Linux GCC build and tests
- [ ] Linux Clang build and tests  
- [ ] macOS build and tests
- [ ] Windows build and tests
- [ ] AddressSanitizer job
- [ ] UndefinedBehaviorSanitizer job
- [ ] Lint checks
- [ ] Unit test coverage ≥80%

**Security-Sensitive PRs Additionally Require:**
- [ ] ThreadSanitizer job (if multithreaded code)
- [ ] MemorySanitizer job (if handling sensitive data)
- [ ] Extended fuzz testing
- [ ] Static analysis clean
- [ ] Security officer review

### Optional Checks

**Performance-Sensitive PRs:**
- [ ] Performance regression job
- [ ] Memory usage analysis
- [ ] Benchmark comparison

**Consensus Changes:**
- [ ] Extended functional test run
- [ ] Cross-platform consensus verification
- [ ] Network compatibility testing

## CI Configuration

### GitHub Actions Workflow

**Main Workflow** (`.github/workflows/ci.yml`):
```yaml
name: CI
on: [push, pull_request]

jobs:
  test-linux:
    runs-on: ubuntu-22.04
    strategy:
      matrix:
        compiler: [gcc, clang]
        build_type: [debug, release]
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential libtool autotools-dev automake
      - name: Build
        run: |
          ./autogen.sh
          ./configure --enable-debug
          make -j$(nproc)
      - name: Test
        run: |
          make check
          test/functional/test_runner.py -j4
```

**Sanitizer Workflow** (`.github/workflows/sanitizers.yml`):
```yaml
name: Sanitizers
on: [push, pull_request]

jobs:
  sanitizers:
    runs-on: ubuntu-22.04
    strategy:
      matrix:
        sanitizer: [address, undefined, thread]
    steps:
      - uses: actions/checkout@v4
      - name: Build with sanitizer
        run: |
          ./configure --with-sanitizers=${{ matrix.sanitizer }}
          make -j$(nproc)
      - name: Test
        run: |
          make check
          test/functional/test_runner.py -j2 --timeout-factor=4
```

### Self-Hosted Runners

**Performance Testing**:
- Dedicated hardware for consistent benchmarks
- Isolated environment for accurate measurements
- Historical performance data storage
- Automated regression detection

**Extended Testing**:
- Long-running fuzz tests
- Stress testing scenarios
- Large-scale functional tests
- Cross-platform integration tests

## Failure Handling

### Automatic Retry

**Transient Failures**:
- Network timeouts
- Resource contention
- Flaky tests
- Infrastructure issues

**Retry Policy**:
- Maximum 2 retries for transient failures
- No retry for compilation errors or test failures
- Exponential backoff for resource conflicts
- Manual trigger for infrastructure issues

### Failure Triage

**Immediate Investigation Required**:
- Security sanitizer failures
- Consensus test failures
- Cross-platform compatibility issues
- Performance regressions >10%

**Standard Investigation**:
- Unit test failures
- Functional test failures
- Lint violations
- Build warnings

**Monitoring and Alerting**:
- Slack/Discord notifications for critical failures
- Email alerts for security-related failures
- Dashboard showing CI health metrics
- Weekly CI health reports

## Performance Monitoring

### Benchmark Tracking

**Tracked Metrics**:
- Transaction validation speed
- Block processing time
- Memory usage patterns
- Network message processing
- Cryptographic operation performance

**Regression Thresholds**:
- **Critical**: >20% regression (blocks merge)
- **Major**: 10-20% regression (requires investigation)
- **Minor**: 5-10% regression (monitoring)
- **Noise**: <5% regression (acceptable variance)

### Resource Monitoring

**System Resources**:
- CPU usage during test runs
- Memory consumption peaks
- Disk I/O patterns
- Network bandwidth usage

**CI Infrastructure**:
- Job queue lengths
- Runner availability
- Build time trends
- Test execution time trends

## Maintenance Procedures

### Daily Tasks

**Automated**:
- [ ] Monitor CI job success rates
- [ ] Check for new security vulnerabilities
- [ ] Update dependency versions
- [ ] Archive old build artifacts

**Manual Review**:
- [ ] Investigate any CI failures
- [ ] Review performance trends
- [ ] Check resource utilization
- [ ] Triage flaky tests

### Weekly Tasks

- [ ] Review CI metrics and trends
- [ ] Update CI configuration as needed
- [ ] Plan infrastructure improvements
- [ ] Review and update test timeouts

### Monthly Tasks

- [ ] Comprehensive CI health review
- [ ] Infrastructure capacity planning
- [ ] Security scan of CI environment
- [ ] Update CI documentation

### Quarterly Tasks

- [ ] Major CI infrastructure updates
- [ ] Platform support review and updates
- [ ] Performance baseline updates
- [ ] CI cost optimization review

## Local Development Integration

### Pre-Commit Hooks

**Recommended Setup**:
```bash
# Install pre-commit hooks
pip install pre-commit
pre-commit install

# Run manually
pre-commit run --all-files
```

**Hook Configuration** (`.pre-commit-config.yaml`):
```yaml
repos:
  - repo: local
    hooks:
      - id: lint-python
        name: Lint Python files
        entry: test/lint/lint-python.py
        language: system
        files: \.py$
      - id: lint-shell
        name: Lint shell scripts
        entry: test/lint/lint-shell.sh
        language: system
        files: \.sh$
```

### Local CI Simulation

**Run CI Locally**:
```bash
# Simulate Linux CI job
./autogen.sh
./configure --enable-debug --with-sanitizers=address
make -j$(nproc)
make check
test/functional/test_runner.py

# Quick pre-push check
test/lint/lint-all.sh
make check
test/functional/test_runner.py -j4 --timeout-factor=0.25
```

## CI Security

### Secrets Management

**Protected Secrets**:
- Release signing keys (not stored in CI)
- API tokens for external services
- Infrastructure access credentials
- Notification webhooks

**Security Practices**:
- Minimal secret scope and access
- Regular secret rotation
- Audit logs for secret access
- Encrypted storage for all secrets

### Supply Chain Security

**Dependency Verification**:
- Verify checksums for all downloaded dependencies
- Use pinned versions in CI
- Monitor for dependency vulnerabilities
- Automated dependency updates with testing

**Build Environment Security**:
- Isolated build environments
- Regular security updates
- Access control and monitoring
- Audit logs for all CI activities

## Troubleshooting

### Common CI Issues

**Build Failures**:
- Check for missing dependencies
- Verify compiler version compatibility
- Review recent configuration changes
- Check for platform-specific issues

**Test Failures**:
- Investigate flaky tests
- Check for timing issues
- Verify test isolation
- Review recent code changes

**Performance Issues**:
- Check resource utilization
- Review test parallelization
- Investigate infrastructure bottlenecks
- Optimize slow tests

### Debug Procedures

**Build Debugging**:
```bash
# Verbose build output
make V=1

# Check specific compilation
make src/btqd VERBOSE=1

# Clean build
make clean && make -j$(nproc)
```

**Test Debugging**:
```bash
# Run single test with debugging
test/functional/failing_test.py --loglevel=DEBUG --nocleanup

# Check test logs
cat /tmp/btq_func_test_*/test_framework.log

# Run with Valgrind
valgrind --tool=memcheck src/test/test_btq --run_test=failing_test
```

## Future Improvements

### Planned Enhancements

**Short Term (3 months)**:
- [ ] Improve test parallelization
- [ ] Add more comprehensive fuzz testing
- [ ] Enhance performance regression detection
- [ ] Implement better flaky test handling

**Medium Term (6 months)**:
- [ ] Add hardware security module testing
- [ ] Implement cross-chain compatibility testing
- [ ] Add quantum computer simulation testing
- [ ] Enhance security scanning integration

**Long Term (12 months)**:
- [ ] Implement formal verification integration
- [ ] Add automated security audit integration
- [ ] Develop quantum-resistant CI infrastructure
- [ ] Create comprehensive performance baselines

### Infrastructure Scaling

**Capacity Planning**:
- Monitor job queue lengths and wait times
- Plan for increased test coverage
- Prepare for post-quantum algorithm testing
- Scale infrastructure for community growth

**Cost Optimization**:
- Optimize test execution times
- Use spot instances where appropriate
- Implement intelligent test selection
- Cache build artifacts effectively

## Metrics and Reporting

### Key Performance Indicators

**Quality Metrics**:
- Test pass rate (target: >99%)
- Time to detect regressions (target: <1 hour)
- False positive rate (target: <1%)
- Security issue detection rate

**Performance Metrics**:
- Average build time (target: <30 minutes)
- Test execution time (target: <60 minutes)
- Resource utilization efficiency
- Infrastructure cost per build

**Developer Experience**:
- Time to feedback (target: <15 minutes)
- CI reliability (target: >99% uptime)
- Documentation completeness
- Ease of debugging failures

### Reporting

**Daily Reports**:
- CI health dashboard
- Failed job summary
- Performance trend alerts
- Security scan results

**Weekly Reports**:
- Test coverage trends
- Performance regression analysis
- Infrastructure utilization
- Flaky test identification

**Monthly Reports**:
- CI effectiveness analysis
- Infrastructure cost review
- Security posture assessment
- Developer satisfaction metrics

## Integration with Development Workflow

### PR Requirements

**Automatic Checks**:
- All CI jobs must pass
- Code coverage must not decrease
- Performance regressions must be justified
- Security scans must be clean

**Manual Review Requirements**:
- Security officer review for crypto changes
- Performance expert review for optimization claims
- Platform expert review for platform-specific changes

### Release Integration

**Release Branch CI**:
- Extended test runs
- Cross-platform compatibility verification
- Performance baseline establishment
- Security audit integration

**Tag Verification**:
- Verify all tests pass on tagged commits
- Run extended test suite
- Generate reproducible builds
- Validate signatures and checksums

## Documentation and Training

### CI Documentation

**For Developers**:
- How to interpret CI results
- Debugging failed CI jobs
- Local CI simulation
- Performance testing guidelines

**For Maintainers**:
- CI configuration management
- Adding new test jobs
- Infrastructure maintenance
- Security incident response

### Training Materials

**New Contributor Onboarding**:
- CI overview and philosophy
- How to fix common CI failures
- Testing best practices
- Performance considerations

**Advanced Topics**:
- Sanitizer usage and debugging
- Fuzz testing development
- Cross-platform testing
- Security testing methodologies

---
