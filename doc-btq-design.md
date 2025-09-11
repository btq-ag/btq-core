📜 BTQ Release Roadmap (Phase 2: Dilithium Integration into PPK Infrastructure)
Objectives

Integrate Dilithium post-quantum signatures into PPK infrastructure.

Maintain a Bitcoin-like release and contribution process for legitimacy.

Balance confidentiality (internal development, private testing) with transparent public releases.

Establish SOPs for contribution, review, communication, and release sharing.

Phase 2 Release Workflow

Development (Private First, Public Second)

Development branches (feature/dilithium-ppk) maintained internally.

Security-sensitive code (signature handling, serialization) is kept private until audited.

A public mirror branch (public-preview/dilithium) contains sanitized changes for external review.

Testing & Hardening

Run internal unit tests, integration tests, and regtest scenarios.

Independent reviewers inside the core team must approve before external push.

Public Release Candidate

Publish a Release Candidate (RC) tag on GitHub (following Bitcoin naming: v0.21.0-rc1-btq).

Announce on Telegram + GitHub discussions for external testers.

Stable Release

After RC feedback, publish signed release binaries.

Provide SHA256SUMS + GPG signatures, exactly like Bitcoin Core.

Announce on GitHub releases and pinned Telegram messages.

🔧 SOPs
1. Pull Requests (PRs)

Policy

All PRs must target public-preview/* branches.

Security-sensitive code is merged internally first, then mirrored.

Process

PR template (mirroring Bitcoin Core):

Motivation / rationale

Implementation notes

Testing instructions

Potential risks / security considerations

Rules

No merge commits → squash + rebase only.

Must pass CI (build, unit tests, lint, functional tests).

Must have at least 2 ACKs (code review approvals) before merge.

2. Code Review

Philosophy (Borrowed from Bitcoin Core)

“Review is more important than writing code.”

Slow, deliberate process → prioritize safety and correctness.

Process

Every PR tagged with labels: Consensus, P2P, Wallet, Docs.

Reviewers comment inline, using NACK (not acceptable), Concept ACK (agree with idea), Approach ACK, Tested ACK.

Security-sensitive code (signature handling, cryptography) requires at least one domain expert ACK.

All review discussions must remain on GitHub to preserve transparency (no Telegram / DM reviews).

3. Communication

Internal

Core dev chat → private, end-to-end encrypted.

Sensitive design discussions → kept internal until stable.

Public

GitHub Discussions for roadmap, feedback, and general Q&A.

GitHub Issues for bugs, not for roadmap debates.

Telegram → community engagement, announcements, answering questions, not for deep design.

Rules

No release-critical info leaked early on Telegram.

All technical decisions must appear on GitHub before being binding.

4. Release Announcements (Telegram SOP)

When

Only after GitHub release is tagged and binaries uploaded.

How

Pinned announcement message in the official BTQ Telegram channel.

Format:

Release version + link to GitHub release

Short changelog summary

Security notes (if any)

Verification instructions (SHA256SUMS + GPG signature)

Example

🚀 BTQ Release v0.21.0-btq
- Added Dilithium PQ signatures to PPK infrastructure
- Increased max tx size for PQ cryptography
- Disabled legacy witness serialization

🔑 Verify: https://github.com/BTQ/releases/tag/v0.21.0
SHA256SUMS + GPG signatures included.


Follow-up

Moderators direct technical discussions back to GitHub.

Telegram is for awareness, not governance.

✅ Summary

This roadmap keeps Bitcoin Core’s credibility model (slow, reviewed, deliberate) while giving you the control knobs for confidentiality.

PRs: strict templates, squash merges, ACK/NACK process.

Code Review: layered ACKs, with domain experts required for crypto.

Communication: GitHub = decisions, Telegram = announcements.

Releases: mirror Bitcoin Core (tags, RCs, GPG signatures), with public Telegram posts only after GitHub release.

Do you want me to also draft the actual GitHub PR template and release checklist (like Bitcoin Core uses) so your repo is ready to enforce this immediately?

You said:
That was nowhere near detailed enough. I need you to write about 10x the amount of content than that. Can you rewrite it and be WAY more detailed. 
ChatGPT said:
Request for GPT-5 Pro
Reasoned for 8m 54s

Below is a Bitcoin‑Core–style public‑release roadmap for BTQ Phase 2 (Dilithium in the PPK infrastructure). It is written as an operating manual you can drop into your repo (split across CONTRIBUTING.md, doc/release-process.md, SECURITY.md, .github/ISSUE_TEMPLATE/, .github/PULL_REQUEST_TEMPLATE.md, etc.). It balances confidentiality with fully public, Bitcoin‑like mechanics (versioning, tags, RCs, signed binaries, deterministic builds, ACK/NACK review culture, etc.).

Where something claims “this is how Bitcoin Core does it,” I cite the relevant place in Core’s docs so you can see the precedent (e.g., versioning, RC cadence, Guix builds, responsible disclosure, ACK taxonomy). 
Bitcoin Core
GitHub
+2
GitHub
+2
Bitcoin Optech

0) Release philosophy (align with Bitcoin Core)

Principles to copy:

Slow, deliberate, review‑heavy development. Testing and review are the bottlenecks; treat them as the project’s main work streams. 
Casey

MAJOR.MINOR versioning with RC suffixes (rc1, rc2, …); tag final as vX.Y. (No hyphen before rc; tags are signed.) 
Bitcoin Core

Deterministic (reproducible) builds with Guix and multi‑party attestations. 
GitHub
Bitcoin Optech

Release candidates first, then stable, with public calls to test RCs and a pinned meta‑issue. 
GitHub

Responsible security disclosure via a documented process and signed advisories. 
Bitcoin Core
Casey

Culture & vocabulary of review: Concept ACK / Approach ACK / (Tested|ut)ACK / NACK; merge decisions rely on quality review, not “votes.” 
GitHub

1) Governance & roles (minimal, Bitcoin‑style)
Role	Responsibilities
Maintainers	Gate merges to main and release branches; run merge scripts; enforce ACK/NACK culture and CI gates. (BTQ mirrors Core’s maintainer‑led merges.) 
GitHub

Release Manager (RM)	Owns Phase 2 release branch; schedules freezes; cuts tags (rc1, rc2, final); coordinates Guix builds & signatures; publishes release notes. 
GitHub

Release Signers	Build & sign binaries (Guix), produce checksums/attestations; sign Git tags. 
GitHub

Security Officers	Triage private reports, run embargoes, coordinate fixes, publish advisories. 
Bitcoin Core

CI Owners	Keep CI matrix green; maintain test infra (unit/functional/fuzz/lint). 
GitHub

Triage Team	Labels issues/PRs, manages milestones, tracks “release‑blocker”, “needs‑backport”, etc. (Backport labels mirror Core.) 
mirror.b10c.me

Comms Leads	GitHub Discussions/release notes; telegram announcements (announcement‑only channel) linking to GitHub release.
2) Branching & versioning strategy

Branches

main: integration/staging (like Core’s).

release/X.Y: cut at feature freeze; receives only release blockers and approved backports.

Maintenance branches: X.Y (e.g., 1.0, 1.1) for minor fixes; use labels backport [X.Y]. 
mirror.b10c.me

Tags

v1.1.0rc1, v1.1.0rc2, … then v1.1.0. (No hyphen before rc.) 
Bitcoin Core

Phase 2 target names (example)

RC: v1.1.0rc1 (Dilithium in PPK).

Stable: v1.1.0.

Semantics

MAJOR for consensus or network‑level changes (not in Phase 2—Dilithium wiring into PPK is policy & infra; consensus activation comes later).

MINOR for new features & deprecations (Phase 2).

PATCH for hotfixes.

3) Confidential‑but‑public: how to publish without leaking secrets

Model: Develop sensitive crypto patches privately, publish sanitized public mirrors early enough for broad review, and keep all decision‑making and merging public.

Private working repos: btq-secure/dilithium-ppk (invite‑only).

Public preview branch: mirror to public-preview/dilithium regularly (strip any internal tooling or comments that leak infra or key material).

No private “out‑of‑band” merges: Maintainers merge only PRs reviewed in public (public preview branch PRs), just as Core requires review on GitHub. 
GitHub

Security diffs: If a security fix is needed, use the embargo flow (Section 11), then publish patches with signed advisories, mirroring Core’s policy. 
Bitcoin Core

4) Issue taxonomy & labels (copy Core’s style)

Area labels: Consensus, P2P, Mempool/Policy, Script/Interpreter, Wallet, RPC, Build/Guix, GUI (if applicable), Tests: functional|unit|fuzz, Docs.

Severity/process: release-blocker, needs backport [1.1], good first issue, help wanted. Core uses explicit backport labels by branch. 
mirror.b10c.me

5) SOP — Pull Requests (how to accept PRs)

This mirrors Core’s contributor model: open contributor model, peer review first, maintainers merge. 
GitHub

5.1 PR entry criteria

One focused change per PR. Explain motivation and approach.

Tests are mandatory for all code changes (unit and, where applicable, functional/fuzz). 
GitHub

All CI jobs must be green (Linux/macOS/Windows; sanitizer jobs; lint). (Lint & test folders reflect Core’s structure.) 
GitHub
Casey

Docs (RPC help, release-notes.md fragment) included when changing interfaces.

Size discipline: Prefer several small PRs over one huge PR.

5.2 PR template (drop into .github/PULL_REQUEST_TEMPLATE.md)
### What
Short title (<=70 chars).

### Why (motivation)
Explain user-visible change or technical debt resolved.

### Approach
High-level design, trade-offs, alternatives considered.

### Testing
- [ ] Unit tests added
- [ ] Functional test(s) added/updated
- [ ] Fuzz target(s) added/updated (if parsing/validation touched)
How to run: `ctest -R`, `test/functional/test_runner.py -j4`, `src/test/fuzz/fuzz <target>`

### Risks / Compatibility
Migration notes, defaults, flags, config interactions.

### Release notes
1–3 bullet points for `doc/release-notes.md`.

### Checkboxes
- [ ] CI green
- [ ] Docs/RPC help updated
- [ ] Benchmarks (if perf-sensitive)

5.3 PR routing & metadata

Use labels for area/severity. Assign milestone 1.1.0 or 1.1.x.

Link design notes (if any) and prior discussions.

5.4 Merge policy

Require ≥2 quality ACKs (at least one experienced reviewer for consensus/crypto areas).

Recognize ACK taxonomy: Concept ACK, Approach ACK, utACK, Tested ACK, NACK (with rationale). 
GitHub

Maintainers merge via maintainer scripts (Core’s merge flow copies ACK paragraphs into merge commits; keep comments professional—they’re forever). 
Jon Atack

Rebases/squash: Prefer clean history; squash fixups; keep logical commits if they aid review.

6) SOP — Code Review (Bitcoin‑style)
6.1 Culture & stages

Concept ACK: Agree with the idea.

Approach ACK: Agree with the design approach/architecture.

utACK: Reviewed code, didn’t test.

Tested ACK: Reviewed + built + ran tests + manual checks where appropriate.

NACK: Object, with specific reasoning and alternatives. 
GitHub

6.2 Reviewer checklist (adapted from publicly shared Core practices)

Correctness: invariants, error paths, integer overflow, undefined behavior, resource cleanup, thread safety.

Consensus/Policy: Any consensus surface? Is behavior activation‑guarded? Is policy change documented?

Security: Key serialization, signature verification, constant‑time operations, fuzzability of parsers.

Interfaces: RPC/CLI help, backward compatibility, deprecations.

Tests: Unit coverage of branches; functional test exercising end‑to‑end; fuzz target added where input parsing is touched. 
GitHub
Casey

Performance: O(n) vs O(n²) paths, allocations, disk/net IO.

Docs & release notes: Are user‑visible changes documented?

6.3 “How to review” SOP (for new contributors)

Build locally; run unit, functional, fuzz, lint suites. (Folders & runners mirror Core.) 
GitHub
Casey

Provide actionable comments; avoid bikeshedding; retest after changes; upgrade an utACK to Tested ACK if warranted.

Prefer small diffs and logic grouping; suggest splitting if PR is too broad.

Link to specific lines; include commit hash in your ACK (Core’s merge scripts ingest this). 
Jon Atack

7) Test & CI matrix (minimal but Core‑like)

Unit tests: src/test, src/wallet/test, etc.

Functional tests: test/functional RPC+P2P black‑box tests (Python runner). 
GitHub

Fuzz: src/test/fuzz, run with libFuzzer; store crashing seeds; add coverage for new serialization/parsing (Dilithium wire types, descriptors). 
Casey

Lint: test/lint/* (shell, python, docs checks). 
Casey

CI: Linux (gcc/clang, ASan/UBSan), macOS (clang), Windows (MSVC). Run unit+functional+lint; nightly fuzz.

8) Release engineering for Phase 2 (Dilithium in PPK)
8.1 Milestones and gates

Design Freeze (DF)

PPK & script/descriptor shape for Dilithium finalized (no public activation yet).

All public preview PRs labeled Phase2: Dilithium PPK.

Feature Freeze (FF)

Branch release/1.1 from main. Only blockers & docs allowed.

String Freeze (SF) (if GUI / user‑facing strings; optional for headless)

Trigger translation workflow (Transifex), as Core does. 
GitHub

RC build & testing window

Tag v1.1.0rc1. Publish pinned meta‑issue for RC testing; request test reports. 
GitHub

Final

Tag v1.1.0, publish notes, binaries, checksums, signatures.

8.2 Exact steps to cut an RC (adapted from Core)

Bump versions (CMakeLists.txt and any version header), set CLIENT_VERSION_RC. 
Casey

Draft release notes: start from template; host a collaborative draft; link it in announcements and the meta‑issue. 
GitHub

Create pinned RC testing meta‑issue (checklist of platforms and test areas). 
GitHub

Guix deterministic builds: kick off builders; collect attestations. 
GitHub

Sign the tag: git tag -s v1.1.0rc1 with RM’s PGP key; push tag.

8.3 Exact steps to cut a final

Roll in accepted RC fixes; repeat RC as needed (rc2, rc3 …). 
Delving Bitcoin

Generate SHA256SUMS and SHA256SUMS.asc (signed). (Core distributes checksums and signatures with releases.) 
Bitcoin Core

Publish GitHub Release with binaries, notes, verification steps.

Announce (Section 12).

9) Build, signing & verification (Core‑style)

Guix builds for Linux/macOS/Windows; each signer publishes their attestation (JSON or log) alongside SHA256 sums; verify reproducibility across signers. 
GitHub
Bitcoin Optech

Git tag signing (git tag -s); binary signing where platform‑appropriate (detached signatures).

Verification instructions in the Release page (copy Core’s pattern: download SHA256SUMS & .asc, verify sigs, then verify sums). 
Bitcoin Core

10) Backports & hotfixes

Label PRs needing backport as needs backport [1.1]; backport PR title: Backport [1.1]: <original title>. (Core tracks backports by branch label.) 
mirror.b10c.me

Only low‑risk, non‑consensus fixes to maintenance branches.

For security hotfixes, follow the embargo process (Section 11).

11) Security & responsible disclosure (Core‑style)

SECURITY.md: publish a security contact (e.g., security@btq.org), list PGP keys of security officers, describe severity categories & timelines. (Core documents this and maintains advisories.) 
Casey
Bitcoin Core

Report intake: ACK receipt within 48h; triage severity; assign handler.

Embargo: private fix development + targeted testing; coordinate with RM for out‑of‑band releases if necessary.

Advisory publication: publish signed advisory with fixed version links; mirror Core’s policy of summarizing advisories on a security page. 
Bitcoin Core

Post‑mortem: add test coverage, fuzz targets, and doc to prevent regressions.

(Bitcoin Core formalized a new disclosure policy in 2024–2025; mirroring that publicly helps trust.) 
Bitcoin Magazine
Google Groups
Bitcoin Core

12) Communication SOP (GitHub‑first, RC calls to test, minimal side channels)

Channels & what they’re for:

GitHub Issues/PRs: All technical discussion & decisions; create pinned meta‑issues for RC testing. 
GitHub

GitHub Discussions: Roadmap Q&A, non‑blocking design talk.

Mailing list (optional): If you stand up a BTQ‑dev list, emulate Core’s bitcoindev list for technical posts only. 
Google Groups

IRC/Matrix/Telegram: community support & announcements; redirect technical decisions back to GitHub.

Norms (borrowed from Core):

Announcements list for releases (you can mirror Core’s pattern). 
GitHub

Keep review/merge traces public on GitHub; no private approvals. 
GitHub

13) Telegram SOP (announcement‑only + verification‑first)

Bitcoin Core does not rely on Telegram; if you use it, keep it strictly for announcements and redirect all technical discussions back to GitHub.

Channel layout

@BTQ_Announcements (read‑only, admins post).

@BTQ_Community (open chat; mods enforce rules).

Posting policy

Post only after the GitHub Release is live; pin the post.

Include: version, short changelog, link to GitHub Release, verification steps (checksums + PGP), link to RC testing meta‑issue (if RC). (Core emphasizes verification and public notes.) 
Bitcoin Core

Moderators reply to questions with links to GitHub issues/notes; no support via DMs.

Templates

RC announcement (pinned):

🚧 BTQ v1.1.0rc1 (Release Candidate)

• Focus: Dilithium integrated into PPK infrastructure (no consensus activation yet)
• Notes & how to test: <GitHub Release link>
• RC testing meta-issue (report results): <link>

Verify before you run:
1) Import signer PGP key(s)
2) Verify SHA256SUMS.asc
3) Verify SHA256SUMS
4) Check binary checksums locally

Technical discussion → GitHub. This chat is for high-level Q&A.


Final announcement (pinned):

🟢 BTQ v1.1.0 released

• What’s new: Dilithium support in PPK infra, test hooks, doc & tools
• Release notes + signed binaries: <GitHub Release link>
• Verify: download SHA256SUMS + SHA256SUMS.asc and validate signatures

Heads-up: Consensus activation for Dilithium is NOT part of this release.

14) Documentation SOP

Keep doc/release-notes.md current; for RCs, maintain a draft and link it in announcements (Core keeps RC notes as drafts linked from announcements/meta-issues). 
GitHub

Add doc/fuzzing.md, doc/testing.md pointers mirroring Core’s structure. 
Casey
+1

If GUI/strings: follow Transifex flow for translations; keep doc/translation_process.md. 
GitHub

15) Phase 2 (Dilithium in PPK) — Release content & acceptance

Scope for v1.1.0:

PPK plumbing for Dilithium (key types, serialization, address/descriptor support), no consensus enforcement yet.

Policy & tooling: RPCs/CLI to generate/inspect Dilithium keys; wallet import/export; descriptor round‑trips.

Limits/tunables: ensure the larger key/sig sizes fit block/tx constraints you defined in Phase 1.

Test vectors: include NIST/KAT vectors where available; cross‑check against a reference implementation.

Exit criteria for RC → Final:

All area owners sign off (Tested ACKs). 
GitHub

CI green; deterministic builds match across ≥2 signers (Linux/macOS/Windows). 
GitHub

No open release-blocker issues on milestone.

16) Phase 2 testing plan (what we ask the public to do)

Functional: wallet keygen/import/export; descriptor derivations for Dilithium; RPC outputs stable; regtest mining unaffected. 
GitHub

Fuzz: key parsing, serialization (wire+disk), descriptor parsing. 
Casey

Performance: measure tx size impact & mempool policy behavior.

Upgrade/downgrade: node running 1.0 → 1.1 with Dilithium keys present; configs consistent; no consensus changes expected.

17) Backport policy specific to Phase 2

Backport only crash fixes, build fixes, and severe wallet bugs to 1.1.x.

Mark PRs with backport [1.1]; cherry‑pick; include “Backport of #<PR>” in commit message. (Mirrors Core’s visible backport workflow.) 
mirror.b10c.me

18) Example release notes skeleton (for doc/release-notes.md)
BTQ v1.1.0 Release Notes
========================

Overview
- Integrates Dilithium into the PPK infrastructure. No consensus activation.

Notable changes
- Keys & Descriptors: add 'dilithium' key type; RPC help updated.
- Policy: larger standard tx/script size ceilings retained from v1.0.
- Build: Guix reproducible builds supported; new build docs.
- Tests: new fuzz targets for key/descriptor parsing; functional wallet tests.

Upgrading
- Compatible with v1.0; no chain reindex required.

Compatibility
- Supported OS: Linux (Kernel 3.17+), macOS 13+, Windows 10+. :contentReference[oaicite:57]{index=57}

Credits
- Contributors list (generated from git between v1.0..v1.1). (Core collects author lists for notes.) :contentReference[oaicite:58]{index=58}

19) RC testing meta‑issue template (pinned issue)
# BTQ v1.1.0rc1 – Community Testing

## What to test
- Dilithium keygen/import/export (RPC + CLI)
- Descriptor round-trips (importdescriptors/getdescriptorinfo)
- Functional tests: `test/functional/test_runner.py -j4`

## Platforms checklist
- [ ] Linux (glibc)
- [ ] macOS (Apple Silicon / Intel)
- [ ] Windows 10+

## Report template
- OS/Arch:
- Build or binary:
- Steps:
- Expected:
- Actual:
- Logs:


(Core pins a meta‑issue for RC testing and links it from announcements.) 
GitHub

20) Security advisory template (signed)
Title: [Low|Medium|High|Critical] – Short description
CVE: CVE-YYYY-XXXXX (if assigned)
Affected: <versions>
Fixed in: vX.Y.Z
Impact: <what an attacker can do>
Workarounds: <if any>
Credits: <reporter>
Timeline: <report date, fix, release, disclosure>
Signatures: PGP signatures of security officers


(Host a Security Advisories page summarizing advisories, like Core.) 
Bitcoin Core

21) Repository files to add/update

CONTRIBUTING.md (open contributor model, review culture; link PR template). 
GitHub

doc/release-process.md (the steps above; based closely on Core’s release process). 
GitHub

contrib/guix/* (builder docs & scripts). 
GitHub

SECURITY.md (contacts, keys, timelines). 
Casey

.github/ISSUE_TEMPLATE/{bug_report,feature_request}.md

.github/PULL_REQUEST_TEMPLATE.md (Section 5.2)

doc/translation_process.md (if GUI/strings). 
GitHub

22) End‑to‑end release checklists
22.1 RC checklist (RM‑owned)

 Feature freeze, cut release/1.1.

 Bump version & CLIENT_VERSION_RC. 
Casey

 Draft release notes + link to collaborative draft. 
GitHub

 Tag & sign v1.1.0rc1.

 Guix builds; collect attestations from ≥2 signers. 
GitHub

 Publish GitHub Release (pre‑release checkbox).

 Open pinned meta‑issue for RC testing. 
GitHub

 Comms: GitHub Discussions + Telegram pinned RC post (template in §13).

22.2 Final checklist

 Address RC issues; repeat RC if needed. 
Delving Bitcoin

 Tag & sign v1.1.0.

 Regenerate SHA256SUMS + SHA256SUMS.asc (signed). 
Bitcoin Core

 Publish GitHub Release (final).

 Announce (GitHub Discussions & Telegram pinned final post).

 Create milestone 1.1.x for maintenance.

23) Practical notes for the Dilithium workstream

Tests before merge: KAT vector verification; cross‑impl vector match; negative tests for corrupted sigs/keys; fuzz harness for key/descriptor parsing. 
Casey

Docs: Precisely document descriptor form and RPC outputs.

No consensus activation yet; keep it policy‑only and UI/infra.

Size limits: enforce and test the Phase‑1 policy ceilings you defined so PQ artifacts fit comfortably.

24) Example label set

Phase2: Dilithium · Consensus · P2P · Script · Wallet · RPC · Build/Guix · Tests:functional · Tests:unit · Tests:fuzz · Docs

release-blocker · needs backport [1.1] · good first issue · help wanted 
mirror.b10c.me

25) Why this is “Bitcoin‑accurate”

Versioning & RC style mirrors Core’s Software Lifecycle and tag conventions (vX.Y, rc1). 
Bitcoin Core

Release process steps (draft notes, RC testing meta‑issue, Transifex, signed artifacts) are lifted from Core’s doc/release‑process. 
GitHub

Deterministic builds (Guix), multi‑party attestations, and signed tags/checksums follow Core practice. 
GitHub
Bitcoin Optech

Review culture & ACK taxonomy are the same as Core’s (Concept ACK, utACK, Tested ACK, NACK). 
GitHub

Security policy mirrors Core’s responsible disclosure & advisories. 
Bitcoin Core
Casey

Test layout (unit, functional, fuzz, lint) and runners map to Core’s directories and docs. 
GitHub
Casey

Appendices
A) Contributor quickstart (1 page you can put at top of CONTRIBUTING.md)

Read Developer Notes (build, style, tests). 
GitHub

Fork & branch from main.

Keep PRs small; include tests; follow template.

Expect to iterate through Concept ACK → Approach ACK → (ut)ACK/ Tested ACK before merge. 
GitHub

B) RC testing guide (public)

Download RC binaries; verify signatures & checksums; run on test machines; report to pinned meta‑issue with logs. (Core emphasizes verification & pinned testing issues.) 
GitHub

What you can do right now

Create the files/templates above in your repo.

Stand up release/1.1; draft notes; open the RC meta‑issue template.

Prep Guix builders among two or more signers (deterministic, multi‑party attestations). 
GitHub

Publish the Telegram rules and pin the announcement templates.

This gives you Bitcoin‑accurate public releases, while letting you keep sensitive crypto development private until you’re ready—without breaking the public protocol of review, RCs, Guix, signatures, and documented security handling.

You said:
Now can you tell me how many .MD files I will need to cover all of these topics? I plan on expanding each of them into their own document. Can you tell me the names of each of these files and I will make them 
ChatGPT said:

Got it. Since you want to expand each area of the SOP into its own .md file, you’ll need a structured documentation set similar to how Bitcoin Core splits things across doc/, .github/, and root-level files.

Here’s a recommended breakdown:

📂 Required .md Files
Root & Meta

README.md
– Project overview, build instructions, links to other docs.

CONTRIBUTING.md
– High-level contributor guide: branching model, PR etiquette, review ACK/NACK culture, testing expectations.

SECURITY.md
– Responsible disclosure policy, contact addresses, PGP keys, advisory publication process.

Release & Governance

RELEASE_PROCESS.md
– Full release workflow: milestones, freezes, tagging, RCs, final builds, Guix reproducibility, signature collection.

RELEASE_NOTES_TEMPLATE.md
– Skeleton for maintainers to generate notes per release (overview, notable changes, compatibility, credits).

RELEASE_CHECKLIST.md
– Step-by-step “do this, then that” for Release Managers (DF/FF, tag, Guix builds, meta-issue, Telegram comms).

GOVERNANCE.md
– Roles (Maintainers, Release Manager, Security Officers, CI Owners), responsibilities, decision-making flow.

Pull Requests & Reviews

PULL_REQUEST_TEMPLATE.md (lives in .github/)
– Motivation, approach, testing, risks, release notes fragment.

CODE_REVIEW_GUIDELINES.md
– What ACK/NACK types mean, reviewer checklists, how to test, quality standards.

CODE_OF_CONDUCT.md
– Community behavior expectations (GitHub + Telegram).

Testing & CI

TESTING_GUIDE.md
– How to run unit, functional, fuzz, and lint suites; expected coverage for new PRs.

FUZZING_GUIDE.md
– How to add fuzz targets, run with libFuzzer, handle crash seeds.

CI_GUIDE.md
– CI matrix (Linux/macOS/Windows), sanitizer jobs, nightly fuzz runs.

Communication

COMMUNICATION.md
– Official channels (GitHub Issues, Discussions, Telegram, mailing list), what goes where, norms.

TELEGRAM_POLICY.md
– Telegram announcement SOP: templates for RC/final, rules for moderators, redirecting to GitHub.

Backports & Maintenance

BACKPORT_POLICY.md
– How to label (needs backport [X.Y]), open backport PRs, criteria for acceptance.

SECURITY_ADVISORY_TEMPLATE.md
– Markdown skeleton for publishing advisories (impact, affected, fixed in, credits, signatures).

Developer Notes (Optional Extras)

DEVELOPER_NOTES.md
– General dev workflow: style, build notes, repo layout. (Bitcoin Core has doc/developer-notes.md.)

TRANSLATION_PROCESS.md (only if you add GUI/strings)
– Instructions for string freeze, Transifex, syncing translations.

✅ Summary

That’s 19 .md files in total (17 core + 2 optional).

They map cleanly to the SOPs we built:

Contribution & Review → CONTRIBUTING.md, PULL_REQUEST_TEMPLATE.md, CODE_REVIEW_GUIDELINES.md.

Releases → RELEASE_PROCESS.md, RELEASE_NOTES_TEMPLATE.md, RELEASE_CHECKLIST.md.

Governance → GOVERNANCE.md.

Security → SECURITY.md, SECURITY_ADVISORY_TEMPLATE.md.

Testing/CI → TESTING_GUIDE.md, FUZZING_GUIDE.md, CI_GUIDE.md.

Communication → COMMUNICATION.md, TELEGRAM_POLICY.md.

Maintenance → BACKPORT_POLICY.md.

Meta & Extras → README.md, CODE_OF_CONDUCT.md, DEVELOPER_NOTES.md, TRANSLATION_PROCESS.md.

Do you want me to draft the folder structure + boilerplate headings for each of these .md files so you can scaffold them instantly in your repo?

You said:
Yes can you please draft the folder structure + boilerplate headings
ChatGPT said:

Perfect — let’s scaffold the full folder structure and boilerplate headings for all the .md files I listed. This way, you can copy-paste the structure straight into your repo and then expand content under each heading.

📂 Suggested Folder Structure
BTQ-Docs/
│
├── README.md
├── CONTRIBUTING.md
├── SECURITY.md
├── GOVERNANCE.md
├── CODE_OF_CONDUCT.md
│
├── releases/
│   ├── RELEASE_PROCESS.md
│   ├── RELEASE_NOTES_TEMPLATE.md
│   ├── RELEASE_CHECKLIST.md
│   ├── BACKPORT_POLICY.md
│   ├── SECURITY_ADVISORY_TEMPLATE.md
│
├── reviews/
│   ├── CODE_REVIEW_GUIDELINES.md
│   └── PULL_REQUEST_TEMPLATE.md   (also placed in .github/)
│
├── testing/
│   ├── TESTING_GUIDE.md
│   ├── FUZZING_GUIDE.md
│   ├── CI_GUIDE.md
│
├── communication/
│   ├── COMMUNICATION.md
│   ├── TELEGRAM_POLICY.md
│
└── dev/
    ├── DEVELOPER_NOTES.md
    └── TRANSLATION_PROCESS.md   (optional, only if GUI/strings exist)


(You’ll also place PULL_REQUEST_TEMPLATE.md under .github/ for GitHub to auto-load it.)

📑 Boilerplate Headings
README.md
# BTQ (Bitcoin Quantum)

## Overview
- What BTQ is
- Roadmap phases

## Getting Started
- Build instructions
- Running regtest/testnet
- Links to docs

## Contributing
- Quickstart link to CONTRIBUTING.md

CONTRIBUTING.md
# Contributing to BTQ

## Philosophy
## How to Propose Changes
## Branching Model
## Commit Standards
## PR Workflow (link to PULL_REQUEST_TEMPLATE.md)
## Testing Expectations
## Release Notes Fragments

SECURITY.md
# Security Policy

## Supported Versions
## Reporting a Vulnerability
## PGP Keys
## Disclosure Timeline
## Advisory Publication

GOVERNANCE.md
# Governance

## Roles
- Maintainers
- Release Manager
- Security Officers
- CI Owners

## Decision-Making
## Conflict Resolution
## Changes to Governance

CODE_OF_CONDUCT.md
# Code of Conduct

## Our Standards
## Unacceptable Behavior
## Enforcement
## Reporting Violations

releases/RELEASE_PROCESS.md
# Release Process

## Overview
## Branching Strategy
## Milestones: DF / FF / SF
## RC Workflow
## Final Release Workflow
## Guix Deterministic Builds
## Signatures & Verification

releases/RELEASE_NOTES_TEMPLATE.md
# Release Notes Template

## Overview
## Notable Changes
## Upgrading
## Compatibility
## Security Notes
## Credits

releases/RELEASE_CHECKLIST.md
# Release Checklist

## Pre-Release
- Freeze milestones
- Tag RC

## RC
- Build + attestations
- Meta-issue for testing

## Final Release
- Tag
- Binaries + checksums
- Announcements

releases/BACKPORT_POLICY.md
# Backport Policy

## Philosophy
## Labeling Backports
## Workflow
## Criteria for Acceptance

releases/SECURITY_ADVISORY_TEMPLATE.md
# Security Advisory Template

## Title
## CVE
## Affected Versions
## Fixed In
## Impact
## Workarounds
## Credits
## Timeline
## Signatures

reviews/CODE_REVIEW_GUIDELINES.md
# Code Review Guidelines

## ACK/NACK Taxonomy
## Reviewer Checklist
## Review Process
## How to Give Feedback

reviews/PULL_REQUEST_TEMPLATE.md
# Pull Request Template

## What
## Why
## Approach
## Testing
## Risks
## Release Notes

testing/TESTING_GUIDE.md
# Testing Guide

## Unit Tests
## Functional Tests
## Fuzz Tests
## Lint Checks
## Running Test Suites

testing/FUZZING_GUIDE.md
# Fuzzing Guide

## Overview
## Adding New Targets
## Running Fuzz Tests
## Handling Crashes

testing/CI_GUIDE.md
# CI Guide

## CI Matrix
## Linux Jobs
## macOS Jobs
## Windows Jobs
## Sanitizers
## Nightly Fuzz

communication/COMMUNICATION.md
# Communication Policy

## GitHub Issues & PRs
## GitHub Discussions
## Mailing Lists (if any)
## Telegram / IRC
## Decision Archival

communication/TELEGRAM_POLICY.md
# Telegram Policy

## Channels
- Announcements
- Community Chat

## Rules
## Announcement Templates
## Moderator Responsibilities

dev/DEVELOPER_NOTES.md
# Developer Notes

## Repo Layout
## Build Instructions
## Style Guide
## Common Tools

dev/TRANSLATION_PROCESS.md
# Translation Process

## String Freeze
## Transifex Workflow
## Syncing Translations