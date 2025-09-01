## What

[Brief description of the change in 50 characters or less]

## Why (Motivation)

[Explain the problem this PR solves or the improvement it provides]

- What user-visible issue does this address?
- What technical debt does this resolve?
- How does this align with BTQ's roadmap?

## Approach

[Describe your implementation approach and design decisions]

### Design Decisions
- [Key architectural choice 1 and rationale]
- [Key architectural choice 2 and rationale]
- [Trade-offs considered]

### Alternatives Considered
- [Alternative approach 1 and why it wasn't chosen]
- [Alternative approach 2 and why it wasn't chosen]

### Implementation Details
- [Important implementation notes]
- [Complex logic explanations]
- [Dependencies or prerequisites]

## Testing

### Test Coverage
- [ ] Unit tests added for new functionality
- [ ] Functional tests added/updated
- [ ] Fuzz targets added/updated (if parsing/validation code changed)
- [ ] Regression tests added (if fixing a bug)

### Testing Instructions

**Unit Tests:**
```bash
make check
# or specific test:
src/test/test_btq --run_test=your_test_suite
```

**Functional Tests:**
```bash
test/functional/test_runner.py
# or specific test:
test/functional/your_test.py
```

**Manual Testing:**
```bash
# Steps to manually verify the change
btqd -regtest
btq-cli -regtest your_new_command
```

### Platform Testing
- [ ] Linux (specify distribution and version)
- [ ] macOS (specify version)
- [ ] Windows (specify version)

## Risks / Compatibility

### Risk Assessment
**Risk Level**: [Low|Medium|High]

**Potential Risks:**
- [Risk 1 and mitigation strategy]
- [Risk 2 and mitigation strategy]

### Backward Compatibility
- [ ] No breaking changes to existing APIs
- [ ] Configuration file compatibility maintained
- [ ] Network protocol compatibility maintained
- [ ] Wallet file format compatibility maintained

### Migration Notes
[If this change requires user action, document it here]

### Configuration Changes
[Document any new configuration options or changes to existing ones]

## Performance Impact

### Performance Analysis
- [ ] No performance regression in critical paths
- [ ] Memory usage impact assessed
- [ ] Disk I/O impact analyzed
- [ ] Network bandwidth impact considered

### Benchmarks
[Include benchmark results if performance-sensitive]

```
Before: [benchmark results]
After:  [benchmark results]
Change: [percentage improvement/regression]
```

## Security Considerations

### Security Review Required
- [ ] Changes affect consensus validation
- [ ] Changes involve cryptographic operations
- [ ] Changes handle private key material
- [ ] Changes process untrusted input
- [ ] Changes affect network protocol

### Threat Model Impact
[Describe any changes to BTQ's attack surface or threat model]

### Post-Quantum Cryptography
[If relevant, describe impact on PQ crypto implementation]

## Release Notes

[Provide 1-3 bullet points for inclusion in release notes]

### RPC
- `new_rpc_method` added to [purpose]

### Wallet
- [User-visible wallet change]

### Build
- [Build system or dependency change]

### Tests
- [Test infrastructure improvement]

## Documentation Updates

### User Documentation
- [ ] RPC help text updated
- [ ] Configuration documentation updated
- [ ] User guide sections updated
- [ ] Example configurations provided

### Developer Documentation
- [ ] Code comments added/updated
- [ ] Architecture documentation updated
- [ ] API documentation updated
- [ ] Developer guide sections updated

## Dependencies

### New Dependencies
- [Library name]: [version] - [justification for adding]

### Updated Dependencies  
- [Library name]: [old version] → [new version] - [reason for update]

### Removed Dependencies
- [Library name]: [reason for removal]

## Checkboxes

### Code Quality
- [ ] CI is green (all platforms)
- [ ] Code follows project style guidelines
- [ ] No compiler warnings introduced
- [ ] Memory leaks checked (Valgrind/ASan)
- [ ] Static analysis issues resolved

### Documentation
- [ ] Code comments added for complex logic
- [ ] RPC help text updated (if applicable)
- [ ] User documentation updated (if applicable)
- [ ] Developer notes updated (if applicable)

### Security
- [ ] Security review completed (if required)
- [ ] Cryptographic changes reviewed by expert
- [ ] Input validation comprehensive
- [ ] No new attack surfaces introduced

### Testing
- [ ] All existing tests pass
- [ ] New tests added for new functionality
- [ ] Test coverage maintained or improved
- [ ] Manual testing completed

### Performance
- [ ] No performance regressions
- [ ] Benchmarks run (if performance-sensitive)
- [ ] Memory usage analyzed
- [ ] Scalability impact considered

## Additional Context

### Related Issues
- Fixes #[issue number]
- Related to #[issue number]
- Depends on #[PR number]

### Future Work
[Describe any follow-up work this change enables or requires]

### Breaking Changes
[List any breaking changes and migration path]

---

## For Reviewers

### Review Focus Areas
[Highlight specific areas where you'd like focused review]

### Known Issues
[Any known limitations or issues with the current implementation]

### Testing Requests
[Specific testing scenarios you'd like reviewers to focus on]

---

*Please ensure you've filled out all relevant sections before submitting your PR. Remove any sections that don't apply to your change.*
