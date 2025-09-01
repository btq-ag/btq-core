# Security Policy

## Supported Versions

We provide security updates for the following BTQ versions:

| Version | Supported          | End of Life |
| ------- | ------------------ | ----------- |
| 1.1.x   | ✅ Current         | TBD         |
| 1.0.x   | ✅ Maintenance     | 2025-12-31  |
| 0.1.x   | ⚠️ Critical only   | 2024-12-31  |

## Reporting a Vulnerability

### Reporting Process

**DO NOT** report security vulnerabilities through public GitHub issues.

Instead, please email us at: **oscar@btq.com** or **barney@btq.com** 

Include the following information:
- Description of the vulnerability
- Steps to reproduce
- Potential impact assessment
- Suggested fix (if any)
- Your contact information for follow-up

### Response Timeline

We are pretty responsive, so we will try to meet these timelines with regards to responding to security reports.

- **Initial Response**: Within 48 hours
- **Severity Assessment**: Within 7 days
- **Fix Development**: 30-90 days depending on complexity
- **Public Disclosure**: After fix is deployed and users have time to upgrade

### Severity Categories

#### Critical (CVSS 9.0-10.0)
- Remote code execution
- Consensus failures leading to chain splits
- Private key extraction
- **Response**: Immediate embargo, emergency release

#### High (CVSS 7.0-8.9)
- Denial of service attacks
- Transaction malleability
- Wallet fund loss
- **Response**: 30-day embargo, coordinated release

#### Medium (CVSS 4.0-6.9)
- Information disclosure
- Minor protocol violations
- Performance degradation
- **Response**: 60-day embargo, next scheduled release

#### Low (CVSS 0.1-3.9)
- Minor bugs with limited impact
- **Response**: Public issue after fix is ready

## PGP Keys

### Security Officers

**Primary Security Contact**
```
Name: BTQ Security Team
Email: oscar@btq.org
Fingerprint: [TO BE ADDED]
```

**Release Manager**
```
Name: [TO BE ADDED]
Email: [TO BE ADDED]
Fingerprint: [TO BE ADDED]
```

### Key Verification

1. Download keys from our website: https://btq.org/security-keys.txt
2. Verify fingerprints through multiple channels
3. Import keys: `gpg --import security-keys.txt`

## Disclosure Timeline

### Standard Process

1. **Day 0**: Vulnerability reported
2. **Day 1-2**: Initial response and triage
3. **Day 7**: Severity assessment and embargo timeline set
4. **Day 30-90**: Fix development and testing
5. **Day 90-120**: Coordinated disclosure and release
6. **Day 120+**: Public advisory publication

### Emergency Process (Critical Vulnerabilities)

1. **Hour 0**: Report received
2. **Hour 6**: Emergency team assembled
3. **Day 1**: Fix development begins
4. **Day 3-7**: Emergency release prepared
5. **Day 7**: Public release and advisory

## Advisory Publication

### Advisory Format

```markdown
# BTQ Security Advisory [YEAR]-[NUMBER]

**Title**: [Vulnerability Description]
**CVE**: CVE-YYYY-XXXXX (if assigned)
**Severity**: [Critical|High|Medium|Low]
**Affected Versions**: [version ranges]
**Fixed In**: vX.Y.Z
**Published**: YYYY-MM-DD

## Impact
[What an attacker can achieve]

## Description
[Technical details of the vulnerability]

## Workarounds
[Mitigation steps if available]

## Solution
[How the fix addresses the issue]

## Credits
[Reporter acknowledgment]

## Timeline
- YYYY-MM-DD: Initial report
- YYYY-MM-DD: Fix developed
- YYYY-MM-DD: Release published
- YYYY-MM-DD: Public disclosure

## Signatures
[PGP signatures from security officers]
```

### Publication Channels

- GitHub Security Advisories
- BTQ website security page
- Mailing list notifications
- Telegram announcements (link to GitHub)

## Security Best Practices

### For Users

- **Verify Downloads**: Always check SHA256SUMS and GPG signatures
- **Keep Updated**: Install security updates promptly
- **Secure Setup**: Use proper firewall and access controls
- **Backup Wallets**: Maintain secure, encrypted backups

### For Developers

- **Code Review**: All security-sensitive code requires expert review
- **Testing**: Include negative test cases and edge conditions
- **Dependencies**: Keep third-party libraries updated
- **Secrets**: Never commit private keys or sensitive configuration

## Responsible Disclosure Guidelines

### For Security Researchers

- **Good Faith**: Research conducted in good faith with intent to improve security
- **Scope**: Focus on BTQ Core software, not infrastructure attacks
- **No Harm**: Do not access, modify, or delete user data
- **Disclosure**: Work with us on responsible timeline before public disclosure

### Recognition

- Security researchers will be credited in advisories (unless they prefer anonymity)
- Significant contributions may be eligible for bounty rewards
- Hall of fame recognition on BTQ website

## Emergency Contacts

For urgent security matters requiring immediate attention:

- **Primary**: oscar@btq.org
- **Backup**: [TO BE ADDED]
- **Signal**: [TO BE ADDED] (for encrypted communication)

## Security Audit History

| Date | Scope | Auditor | Report |
|------|-------|---------|---------|
| TBD  | Full codebase | TBD | TBD |

## Additional Resources

- [Bitcoin Core Security Policy](https://github.com/bitcoin/bitcoin/blob/master/SECURITY.md)
- [CVE Database](https://cve.mitre.org/)
- [NIST Post-Quantum Cryptography](https://csrc.nist.gov/projects/post-quantum-cryptography)

---