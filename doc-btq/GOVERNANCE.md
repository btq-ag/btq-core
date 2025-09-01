# BTQ Governance

## Overview

BTQ follows a maintainer-led governance model similar to Bitcoin Core, emphasizing technical merit, peer review, and consensus-building over formal voting. Decision-making is transparent, documented, and focused on the long-term health of the project.

## Roles and Responsibilities

### Maintainers

**Primary Responsibilities:**
- Gate merges to `master` and release branches
- Enforce code review standards and ACK/NACK culture
- Run merge scripts and maintain clean commit history
- Resolve conflicts between contributors
- Ensure CI requirements are met before merge

**Current Maintainers:**
- oscar@btq.com
- barney@btq.com

**Becoming a Maintainer:**
- Demonstrate consistent, high-quality contributions over 6+ months
- Show expertise in multiple areas of the codebase
- Exhibit good judgment in code review and conflict resolution
- Nomination and approval by existing maintainers

### Release Manager (RM)

**Primary Responsibilities:**
- Own release branches and coordinate release schedules
- Schedule feature freezes and string freezes
- Cut release candidate and final tags
- Coordinate Guix builds and signature collection
- Publish release notes and coordinate announcements

**Current Release Manager:**
- oscar@btq.com
- barney@btq.com

**Term:** 1 year, renewable

### Security Officers

**Primary Responsibilities:**
- Triage private security reports
- Coordinate embargo periods and responsible disclosure
- Develop and test security fixes
- Publish signed security advisories
- Maintain security documentation and processes

**Current Security Officers:**
- oscar@btq.com
- barney@btq.com

**Requirements:**
- Demonstrated expertise in cryptography or security
- PGP key for signing advisories and communications
- Ability to respond to urgent security matters

### CI Owners

**Primary Responsibilities:**
- Maintain CI infrastructure and test matrix
- Ensure test suites remain comprehensive and fast
- Debug CI failures and flaky tests
- Update build dependencies and toolchains

**Current CI Owners:**
- oscar@btq.com
- barney@btq.com

### Triage Team

**Primary Responsibilities:**
- Label issues and PRs with appropriate area/priority tags
- Manage milestones and release planning
- Track release-blocker and needs-backport items
- Help new contributors navigate the process

**Current Triage Team:**
- oscar@btq.com
- barney@btq.com

### Communication Leads

**Primary Responsibilities:**
- Manage GitHub Discussions and community engagement
- Coordinate release announcements across channels
- Maintain communication policies and templates
- Moderate community channels

**Current Communication Leads:**
- oscar@btq.com
- barney@btq.com

## Decision-Making Process

### Technical Decisions

1. **Proposal**: Issues or PRs with clear motivation and approach
2. **Discussion**: Public discussion on GitHub with technical merit focus
3. **Review**: Code review following ACK/NACK taxonomy
4. **Consensus**: Rough consensus among qualified reviewers
5. **Implementation**: Merge after requirements met

### Consensus Requirements

- **Consensus Changes**: Require broad agreement and activation mechanism
- **Policy Changes**: Need clear rationale and compatibility analysis
- **Security Changes**: Require security officer involvement
- **Breaking Changes**: Need migration path and documentation

### Conflict Resolution

1. **Technical Disagreements**: Resolved through evidence, benchmarks, and expert opinion
2. **Process Disputes**: Escalated to maintainers for final decision
3. **Code of Conduct Issues**: Handled by designated moderators
4. **Appeal Process**: Available for all decisions with clear rationale required

## Review Culture

### ACK/NACK Taxonomy

**Concept ACK**: "I agree this is a problem worth solving"
**Approach ACK**: "I agree with the proposed solution approach"
**utACK**: "I reviewed the code and it looks correct" (untested ACK)
**Tested ACK**: "I reviewed, built, and tested the changes"
**NACK**: "I object to this change" (must include specific reasoning and alternatives)

### Review Standards

- **Quality over Speed**: Thorough review is more important than fast merges
- **Constructive Feedback**: Focus on technical merit, not personal preferences
- **Expertise Matching**: Complex changes require review from domain experts
- **Public Discussion**: All technical decisions must happen in public on GitHub

### Review Requirements by Area

| Area | Required Reviewers | Special Requirements |
|------|-------------------|---------------------|
| Consensus | ≥2 maintainers | Activation mechanism required |
| Cryptography | ≥1 crypto expert | Constant-time analysis |
| P2P | ≥1 network expert | Protocol compatibility |
| Wallet | ≥1 wallet expert | Backup/recovery testing |
| RPC | ≥1 API expert | Backward compatibility |
| Tests | ≥1 test expert | Coverage requirements |

## Changes to Governance

### Amendment Process

1. **Proposal**: Open GitHub Discussion with detailed rationale
2. **Community Input**: 30-day comment period
3. **Maintainer Review**: Technical feasibility and implementation plan
4. **Consensus Building**: Address concerns and build agreement
5. **Implementation**: Update governance documents
6. **Announcement**: Communicate changes to community

### Emergency Changes

In case of urgent security or operational needs:
- Maintainers may implement temporary governance changes
- Changes must be ratified through normal process within 60 days
- Community must be notified immediately with rationale

## Maintainer Succession

### Adding Maintainers

1. **Nomination**: Any current maintainer may nominate a contributor
2. **Evaluation Period**: 30-day review of nominee's contributions and judgment
3. **Consensus**: All current maintainers must approve (no vetoes)
4. **Onboarding**: Access grants and responsibility transfer

### Removing Maintainers

1. **Voluntary**: Maintainers may step down at any time
2. **Involuntary**: For conduct violations or extended inactivity
3. **Process**: Requires consensus among remaining maintainers
4. **Appeal**: Available through community discussion process

## Communication Channels

### Primary Channels

- **GitHub Issues**: Bug reports and feature requests
- **GitHub PRs**: Code review and technical discussion
- **GitHub Discussions**: Design discussions and Q&A

### Secondary Channels

- **Telegram**: Community announcements and general discussion
- **IRC/Matrix**: Developer coordination (if established)
- **Mailing Lists**: Release announcements (if established)

### Channel Policies

- **Technical Decisions**: Must happen on GitHub for transparency
- **No Private Deals**: All agreements must be public and documented
- **Respectful Communication**: Follow Code of Conduct across all channels

## Transparency and Accountability

### Public Records

- All governance decisions documented in GitHub
- Meeting notes published (if formal meetings established)
- Conflict resolutions documented with rationale
- Changes to governance tracked in git history

### Regular Reviews

- **Quarterly**: Review open issues and PR backlog
- **Annually**: Assess governance effectiveness and needed changes
- **Post-Release**: Conduct retrospectives on release process

### Community Oversight

- Community members may raise concerns about governance
- Regular opportunities for feedback through GitHub Discussions
- Open process for proposing governance improvements

---
