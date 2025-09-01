# BTQ Code Review Guidelines

## Philosophy

Code review is the most important activity in BTQ development. Every line of code that enters the repository must be thoroughly reviewed by qualified contributors. We prioritize security, correctness, and maintainability over development speed.

### Core Principles

- **Security First**: Post-quantum cryptography requires exceptional care
- **Quality Over Speed**: Thorough review prevents bugs and vulnerabilities
- **Knowledge Sharing**: Reviews are opportunities to learn and teach
- **Constructive Feedback**: Focus on technical merit, not personal style
- **Collaborative Process**: Work together to improve code quality

## ACK/NACK Taxonomy

### Concept ACK
**Meaning**: "I agree this problem should be solved"
**When to Use**: Early in PR lifecycle, before implementation review
**Example**: "Concept ACK - this memory leak needs to be fixed"

### Approach ACK  
**Meaning**: "I agree with the proposed solution approach"
**When to Use**: After reviewing design but before detailed code review
**Example**: "Approach ACK - using RAII here is the right solution"

### utACK (Untested ACK)
**Meaning**: "I reviewed the code and it looks correct, but didn't test it"
**When to Use**: When you've done thorough code review but no functional testing
**Requirements**: 
- Read and understood all changed code
- Verified logic correctness
- Checked for obvious bugs or issues
**Example**: "utACK abc123d - code looks correct, good test coverage"

### Tested ACK
**Meaning**: "I reviewed the code AND built and tested the changes"
**When to Use**: After both code review and functional validation
**Requirements**:
- All requirements of utACK
- Built the code locally
- Ran relevant test suites
- Tested new functionality manually (if applicable)
**Example**: "Tested ACK def456g - built and tested on Ubuntu 22.04, all tests pass"

### NACK
**Meaning**: "I object to this change"
**When to Use**: When you believe the change should not be merged
**Requirements**:
- Provide specific technical reasoning
- Suggest alternatives when possible
- Be constructive and professional
**Example**: "NACK - this approach introduces a race condition in line 45, suggest using atomic operations instead"

### Stale Review Handling
- Reviews become stale after significant changes
- Reviewers should re-review after force pushes
- Maintainers may request fresh reviews for old PRs

## Reviewer Checklist

### General Code Quality

**Correctness**
- [ ] Logic is sound and handles edge cases
- [ ] Error handling is comprehensive
- [ ] Resource cleanup is proper (RAII, smart pointers)
- [ ] Thread safety is maintained
- [ ] Integer overflow/underflow prevented

**Security**
- [ ] Input validation is thorough
- [ ] Buffer bounds are checked
- [ ] Cryptographic operations are constant-time
- [ ] Private data is properly cleared
- [ ] Attack surfaces are minimized

**Performance**
- [ ] Algorithms are efficient (check O(n) complexity)
- [ ] Memory usage is reasonable
- [ ] No unnecessary allocations in hot paths
- [ ] Database operations are optimized
- [ ] Network I/O is efficient

**Maintainability**
- [ ] Code follows project style guidelines
- [ ] Functions are appropriately sized
- [ ] Variable names are clear and descriptive
- [ ] Comments explain complex logic
- [ ] Code is well-structured and modular

### Area-Specific Checklists

#### Consensus Code

**Critical Requirements**:
- [ ] Changes are activation-height gated
- [ ] Backward compatibility maintained
- [ ] Consensus tests added/updated
- [ ] Chain state transitions verified
- [ ] Fork handling considered

**Validation Logic**:
- [ ] All validation rules implemented correctly
- [ ] Edge cases handled (empty blocks, maximum sizes)
- [ ] DoS protection maintained
- [ ] Performance impact assessed

#### Cryptographic Code

**Security Requirements**:
- [ ] Constant-time operations verified
- [ ] Side-channel analysis completed
- [ ] Key material properly handled
- [ ] Random number generation is secure
- [ ] Algorithm implementation matches specification

**Post-Quantum Specific**:
- [ ] NIST reference vectors pass
- [ ] Key and signature sizes validated
- [ ] Serialization format documented
- [ ] Cross-implementation compatibility tested

#### P2P Networking

**Protocol Compliance**:
- [ ] Message format follows specification
- [ ] Version negotiation handled correctly
- [ ] Backward compatibility maintained
- [ ] DoS protection implemented

**Network Behavior**:
- [ ] Connection management is robust
- [ ] Bandwidth usage is reasonable
- [ ] Timeout handling is appropriate
- [ ] Error recovery is implemented

#### Wallet Code

**Security**:
- [ ] Private keys handled securely
- [ ] Backup/recovery tested
- [ ] Access control implemented
- [ ] Encryption is strong

**Usability**:
- [ ] Error messages are helpful
- [ ] Edge cases handled gracefully
- [ ] Performance is acceptable
- [ ] Data migration tested

#### RPC Interface

**API Design**:
- [ ] Interface is intuitive and consistent
- [ ] Parameter validation is thorough
- [ ] Error codes are appropriate
- [ ] Help text is accurate and complete

**Compatibility**:
- [ ] Backward compatibility maintained
- [ ] Deprecation notices added (if applicable)
- [ ] Version-specific behavior documented
- [ ] Client libraries considered

#### Testing Code

**Test Quality**:
- [ ] Tests cover new functionality completely
- [ ] Edge cases and error conditions tested
- [ ] Tests are deterministic and reliable
- [ ] Test names are descriptive

**Coverage**:
- [ ] Unit tests for all new functions
- [ ] Integration tests for user-visible changes
- [ ] Regression tests for bug fixes
- [ ] Performance tests for optimizations

## Review Process

### Initial Review

1. **Concept Review**
   - Understand the problem being solved
   - Evaluate if the solution is appropriate
   - Consider alternative approaches
   - Provide Concept ACK or suggest alternatives

2. **Approach Review**
   - Review the implementation strategy
   - Check architectural decisions
   - Evaluate trade-offs made
   - Provide Approach ACK or suggest improvements

### Detailed Code Review

3. **Line-by-Line Review**
   - Read every changed line carefully
   - Check for bugs, security issues, style violations
   - Verify test coverage is adequate
   - Look for opportunities to improve code quality

4. **Testing Review**
   - Build the code locally
   - Run the test suite
   - Test new functionality manually
   - Verify performance claims with benchmarks

### Final Review

5. **Integration Review**
   - Consider impact on rest of codebase
   - Check for unintended side effects
   - Verify documentation is updated
   - Confirm release notes fragment is appropriate

## How to Give Effective Feedback

### Constructive Comments

**Good Examples**:
- "Consider using `std::unique_ptr` here to avoid manual memory management"
- "This function might benefit from input validation for the `amount` parameter"
- "The test coverage looks good, but consider adding a test for the error case on line 123"

**Poor Examples**:
- "This code is bad" (not specific or helpful)
- "Why didn't you use my approach?" (not constructive)
- "This is obviously wrong" (dismissive and unhelpful)

### Technical Focus

**Focus On**:
- Correctness and security
- Performance implications
- Maintainability and readability
- Test coverage and quality
- Documentation accuracy

**Avoid**:
- Personal coding preferences
- Bikeshedding on trivial style issues
- Architectural debates on small PRs
- Off-topic discussions

### Actionable Feedback

**Provide**:
- Specific line numbers and code snippets
- Clear description of the issue
- Suggested improvements or alternatives
- Links to relevant documentation or examples

**Include**:
- Reasoning behind suggestions
- Severity of issues (critical vs. nice-to-have)
- Examples of preferred patterns
- References to similar code in the codebase

## Review Requirements by Change Type

### Consensus Changes

**Required Reviewers**: ≥2 maintainers + consensus expert
**Special Requirements**:
- Activation mechanism must be included
- Comprehensive test coverage required
- Network compatibility analysis
- Performance impact assessment

**Review Focus**:
- Consensus rule correctness
- Activation safety
- Backward compatibility
- Chain state impact

### Security-Sensitive Changes

**Required Reviewers**: ≥1 security officer + area expert
**Special Requirements**:
- Security threat model analysis
- Constant-time operation verification
- Fuzz testing for new parsers
- Side-channel analysis (for crypto code)

**Review Focus**:
- Attack surface analysis
- Input validation completeness
- Cryptographic correctness
- Error handling security

### RPC/API Changes

**Required Reviewers**: ≥1 API expert + maintainer
**Special Requirements**:
- Backward compatibility analysis
- Help text updates
- Client library impact assessment
- Deprecation timeline (if applicable)

**Review Focus**:
- Interface design consistency
- Parameter validation
- Error handling and codes
- Documentation completeness

### Performance-Critical Changes

**Required Reviewers**: ≥1 performance expert
**Special Requirements**:
- Benchmark results included
- Memory usage analysis
- Scalability impact assessment
- Regression test coverage

**Review Focus**:
- Algorithm efficiency
- Memory allocation patterns
- Hot path optimization
- Scalability implications

## Common Review Patterns

### Security Review Patterns

**Input Validation**:
```cpp
// Good: Comprehensive validation
if (input.size() > MAX_SIZE || input.empty()) {
    return error("Invalid input size");
}

// Bad: Missing validation
process_input(input);  // No size check
```

**Memory Safety**:
```cpp
// Good: RAII and bounds checking
std::vector<uint8_t> buffer(size);
if (offset + length > buffer.size()) {
    return false;
}

// Bad: Manual memory management
uint8_t* buffer = new uint8_t[size];  // Potential leak
```

### Performance Review Patterns

**Efficient Algorithms**:
```cpp
// Good: O(log n) lookup
auto it = sorted_map.find(key);

// Bad: O(n) search
for (const auto& item : large_vector) {
    if (item.key == key) break;
}
```

**Memory Efficiency**:
```cpp
// Good: Reserve capacity
std::vector<Item> items;
items.reserve(expected_size);

// Bad: Repeated allocations
std::vector<Item> items;  // Will reallocate multiple times
```

## Review Tools and Automation

### Static Analysis

**Required Checks**:
- Clang static analyzer
- Cppcheck for common bugs
- Valgrind for memory errors
- AddressSanitizer for runtime checks

**Cryptographic Code**:
- Constant-time verification tools
- Side-channel analysis
- Formal verification (where applicable)

### Code Coverage

**Requirements**:
- Maintain or improve line coverage
- Cover all new branches and conditions
- Include negative test cases
- Test error handling paths

**Tools**:
- gcov/lcov for coverage measurement
- Coveralls for coverage tracking
- Manual review of uncovered lines

### Performance Testing

**Benchmarking**:
- Micro-benchmarks for critical functions
- Integration benchmarks for full workflows
- Memory usage profiling
- Network performance testing

## Handling Disagreements

### Technical Disagreements

1. **Present Evidence**: Provide code examples, benchmarks, or research
2. **Seek Expert Opinion**: Involve domain experts in the discussion  
3. **Test Alternatives**: Implement and compare different approaches
4. **Build Consensus**: Work toward agreement through technical merit
5. **Escalate if Needed**: Maintainers make final decisions

### Process Disagreements

1. **Clarify Requirements**: Ensure everyone understands the standards
2. **Reference Documentation**: Point to relevant guidelines
3. **Discuss Improvements**: Suggest process improvements if warranted
4. **Follow Current Process**: Adhere to established procedures while discussing changes

## Reviewer Development

### Becoming a Reviewer

**Prerequisites**:
- Demonstrated understanding of codebase
- History of quality contributions
- Familiarity with review guidelines
- Domain expertise in relevant areas

**Development Path**:
1. **Shadow Reviews**: Comment on PRs to practice review skills
2. **Mentored Reviews**: Work with experienced reviewers
3. **Independent Reviews**: Begin reviewing smaller PRs independently
4. **Expert Reviews**: Graduate to reviewing complex, security-sensitive changes

### Reviewer Responsibilities

**Quality Assurance**:
- Ensure code meets project standards
- Verify test coverage is adequate
- Check documentation is updated
- Validate security considerations

**Knowledge Transfer**:
- Share expertise with other contributors
- Explain reasoning behind feedback
- Help less experienced developers improve
- Document common patterns and anti-patterns

**Process Adherence**:
- Follow review guidelines consistently
- Provide timely feedback on assigned reviews
- Escalate issues appropriately
- Maintain professional communication

---

