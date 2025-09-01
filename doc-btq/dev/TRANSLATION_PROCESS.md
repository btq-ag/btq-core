# BTQ Translation Process

## Overview

BTQ supports internationalization to make quantum-resistant cryptocurrency accessible to users worldwide. This document outlines the translation workflow, tools, and guidelines for managing multilingual support.

## Translation Philosophy

### Principles

- **Accuracy First**: Technical accuracy takes precedence over linguistic fluency
- **Consistency**: Use consistent terminology across all translations
- **Cultural Sensitivity**: Respect cultural differences while maintaining technical precision
- **Community-Driven**: Leverage community expertise for quality translations

### Scope

**Translated Content**:
- GUI interface strings (if GUI exists)
- Error messages and user notifications
- RPC help text and documentation
- Configuration option descriptions
- Critical user-facing messages

**Not Translated**:
- Debug messages and internal logging
- Developer documentation
- Code comments
- Technical specifications

## Translation Workflow

### String Freeze Process

#### Timeline

**String Freeze (SF)**: 4 weeks before release
- All user-facing strings finalized
- No new strings added without translation team approval
- String changes require retranslation coordination

#### String Freeze Checklist

**Pre-Freeze (1 week before SF)**:
- [ ] Review all user-facing strings for clarity
- [ ] Consolidate similar messages
- [ ] Remove unused or duplicate strings
- [ ] Update string documentation and context

**At String Freeze**:
- [ ] Lock string changes in release branch
- [ ] Export strings to translation platform
- [ ] Notify translation teams of deadline
- [ ] Create translation tracking issue

**Post-Freeze**:
- [ ] Monitor translation progress
- [ ] Review and approve translations
- [ ] Import completed translations
- [ ] Test translated interface

### Translation Platform

#### Transifex Integration

**Project Setup**:
- BTQ project on Transifex platform
- Separate resources for different components
- Role-based access control for translators
- Integration with GitHub repository

**Resource Organization**:
```
btq-core/
├── gui-strings          # GUI interface strings
├── rpc-help            # RPC help text
├── error-messages      # User-facing error messages
├── config-options      # Configuration descriptions
└── release-notes       # Release note templates
```

#### Translation Management

**Coordinator Responsibilities**:
- Manage translator access and permissions
- Review translation quality and consistency
- Coordinate with development team on string changes
- Maintain translation documentation and guidelines

**Quality Assurance**:
- Native speaker review for all translations
- Technical accuracy verification
- Consistency checking across resources
- User testing with translated interface

### String Extraction and Integration

#### Source Code String Handling

**String Marking for Translation**:
```cpp
// Good: Translatable string
strprintf(_("Error: %s"), error_message)

// Good: Non-translatable string  
strprintf("Debug: %s", debug_info)

// Context for translators
_("Amount") /* Bitcoin amount, not quantity */
```

**String Context and Comments**:
```cpp
// Provide context for translators
/** TRANSLATORS: This is a confirmation dialog title */
_("Confirm Transaction")

/** TRANSLATORS: %1 is the transaction amount, %2 is the recipient address */
_("Send %1 to %2?")
```

#### Extraction Process

**Extract Strings from Source**:
```bash
# Extract translatable strings
lupdate src/ -ts locale/btq_en.ts

# Update existing translations
lupdate src/ -ts locale/btq_*.ts
```

**String File Management**:
```bash
# Validate translation files
lrelease locale/btq_*.ts

# Generate binary translation files
lrelease locale/btq_*.ts -qm locale/
```

## Translation Guidelines

### Technical Terminology

#### Standardized Terms

**Core Concepts**:
- "blockchain" → [Standard translation or transliteration]
- "transaction" → [Standard financial term in target language]
- "wallet" → [Standard financial term in target language]
- "private key" → [Cryptographic term translation]
- "public key" → [Cryptographic term translation]

**BTQ-Specific Terms**:
- "post-quantum" → [Scientific term translation]
- "Dilithium" → [Keep as proper noun or transliterate]
- "quantum-resistant" → [Scientific term translation]
- "PPK infrastructure" → [Technical term handling]

#### Terminology Database

**Maintain Glossary**:
- Create and maintain terminology database
- Include approved translations for technical terms
- Provide context and usage examples
- Update with new post-quantum terminology

### Translation Quality Standards

#### Accuracy Requirements

**Technical Precision**:
- Maintain exact meaning of technical terms
- Preserve error message specificity
- Keep configuration option clarity
- Ensure help text usefulness

**Linguistic Quality**:
- Natural, fluent target language
- Appropriate register and tone
- Consistent terminology usage
- Proper grammar and spelling

#### Review Process

**Initial Translation**:
1. Translator creates initial translation
2. Self-review for accuracy and fluency
3. Submit for community review
4. Address feedback and revisions

**Quality Review**:
1. Native speaker review for fluency
2. Technical expert review for accuracy
3. Consistency check against glossary
4. User testing if possible

**Final Approval**:
1. Translation coordinator final review
2. Integration testing with BTQ
3. Approval for inclusion in release
4. Documentation of any issues or limitations

## Supported Languages

### Current Languages

**Tier 1 (Full Support)**:
- English (source language)
- [To be determined based on community]

**Tier 2 (Community Maintained)**:
- [Languages with active community translators]

**Tier 3 (Partial Support)**:
- [Languages with incomplete translations]

### Adding New Languages

#### Requirements for New Language Support

**Community Requirements**:
- At least 2 committed native speaker translators
- Technical reviewer familiar with cryptocurrency terminology
- Commitment to maintain translations long-term
- Community size justifying the effort

**Technical Requirements**:
- Language supported by Qt framework (if GUI applicable)
- Proper Unicode support and rendering
- Text direction support (LTR/RTL)
- Font availability for target platforms

#### New Language Process

1. **Community Request**: Create GitHub issue requesting language support
2. **Translator Recruitment**: Identify and verify translator qualifications
3. **Setup**: Create Transifex resource and configure tooling
4. **Initial Translation**: Complete initial translation of core strings
5. **Review and Testing**: Quality review and user testing
6. **Integration**: Add to build system and release process
7. **Maintenance**: Ongoing translation maintenance and updates

## Translation Tools

### Development Tools

#### String Extraction

**Qt Linguist Tools**:
```bash
# Extract strings from Qt sources
lupdate src/qt/ -ts locale/btq_en.ts

# Update all translation files
for lang in de es fr it ja ko zh_CN zh_TW; do
    lupdate src/qt/ -ts locale/btq_$lang.ts
done
```

**Custom Extraction Scripts**:
```bash
# Extract RPC help strings
python3 contrib/devtools/extract-rpc-help.py > rpc-help-strings.pot

# Extract error messages
python3 contrib/devtools/extract-error-messages.py > error-messages.pot
```

#### Translation Validation

**Validation Scripts**:
```bash
# Check translation completeness
python3 contrib/devtools/check-translations.py

# Validate translation files
python3 contrib/devtools/validate-translations.py locale/

# Check for untranslated strings
python3 contrib/devtools/find-untranslated.py
```

### Integration Tools

#### Build System Integration

**CMake Configuration**:
```cmake
# Find Qt translation tools
find_package(Qt5LinguistTools)

# Add translation targets
qt5_add_translation(QM_FILES ${TS_FILES})
add_custom_target(translations ALL DEPENDS ${QM_FILES})
```

**Autotools Integration**:
```makefile
# Translation targets in Makefile.am
EXTRA_DIST += $(TRANSLATION_FILES)

translations: $(TRANSLATION_FILES:.ts=.qm)

%.qm: %.ts
	$(LRELEASE) $< -qm $@
```

## Quality Assurance

### Translation Testing

#### Automated Testing

**String Validation**:
```python
# Test translation file integrity
def test_translation_files():
    for lang_file in glob.glob('locale/*.ts'):
        # Validate XML structure
        tree = ET.parse(lang_file)
        # Check for required elements
        assert tree.find('.//TS') is not None
        # Validate character encoding
        assert is_valid_utf8(lang_file)
```

**UI Testing**:
```python
# Test GUI with different languages
def test_gui_translations():
    for lang in SUPPORTED_LANGUAGES:
        # Start GUI with specific language
        gui = start_btq_gui(language=lang)
        # Verify UI elements render correctly
        assert gui.check_ui_elements()
        # Test critical workflows
        gui.test_basic_operations()
```

#### Manual Testing

**Translation Review Checklist**:
- [ ] All strings translated appropriately
- [ ] Technical terms used consistently
- [ ] UI layout works with translated text
- [ ] Help text is clear and accurate
- [ ] Error messages are understandable

**User Experience Testing**:
- [ ] Native speakers test full workflows
- [ ] Identify confusing or unclear translations
- [ ] Verify cultural appropriateness
- [ ] Test on target platforms and devices

### Quality Metrics

**Translation Completeness**:
- Percentage of strings translated per language
- Time to translate new strings
- Translation consistency scores
- Community translator activity

**Quality Indicators**:
- User feedback on translation quality
- Bug reports related to translations
- Translator satisfaction and retention
- Community adoption of translated versions

## Community Management

### Translator Community

#### Recruitment

**Translator Qualifications**:
- Native or near-native speaker of target language
- Understanding of cryptocurrency and technical concepts
- Familiarity with translation tools and processes
- Commitment to ongoing maintenance

**Recruitment Channels**:
- GitHub Discussions and Issues
- Telegram community announcements
- Cryptocurrency and localization communities
- University partnerships for technical translation

#### Recognition and Incentives

**Recognition Programs**:
- Credits in release notes and documentation
- Translator hall of fame on website
- Special recognition badges or titles
- Community appreciation events

**Potential Incentives**:
- BTQ bounties for quality translations
- Early access to new features
- Direct communication with development team
- Conference speaking opportunities

### Community Coordination

#### Translation Teams

**Team Structure**:
- **Lead Translator**: Coordinates team and maintains quality
- **Translators**: Provide translations and reviews
- **Technical Reviewer**: Ensures technical accuracy
- **Community Liaison**: Connects with broader language community

**Communication**:
- Language-specific channels or groups
- Regular coordination meetings
- Shared documentation and resources
- Direct access to development team

#### Feedback and Improvement

**Feedback Collection**:
- User surveys on translation quality
- Community feedback through GitHub
- Translator feedback on process and tools
- Regular review meetings with translation teams

**Continuous Improvement**:
- Regular process refinement
- Tool and platform improvements
- Training and resource development
- Community building and engagement

## Technical Implementation

### File Organization

```
locale/
├── btq_en.ts           # Source language template
├── btq_de.ts           # German translations
├── btq_es.ts           # Spanish translations
├── btq_fr.ts           # French translations
├── btq_ja.ts           # Japanese translations
├── btq_ko.ts           # Korean translations
├── btq_zh_CN.ts        # Simplified Chinese
├── btq_zh_TW.ts        # Traditional Chinese
└── README.md           # Translation documentation
```

### Integration Points

**GUI Integration** (if applicable):
```cpp
// Language selection in GUI
void SetLanguage(const QString& language) {
    QLocale::setDefault(QLocale(language));
    
    static QTranslator translator;
    translator.load(QString("btq_%1").arg(language), ":/translations/");
    QApplication::installTranslator(&translator);
}
```

**CLI Integration**:
```cpp
// Locale-aware error messages
std::string GetLocalizedError(ErrorCode code) {
    switch (code) {
        case ERROR_INVALID_ADDRESS:
            return _("Invalid address format");
        case ERROR_INSUFFICIENT_FUNDS:
            return _("Insufficient funds for transaction");
        // ...
    }
}
```

## Maintenance and Updates

### Regular Maintenance

**Monthly Tasks**:
- [ ] Review translation progress
- [ ] Update terminology database
- [ ] Coordinate with active translators
- [ ] Address translation-related issues

**Per-Release Tasks**:
- [ ] String freeze coordination
- [ ] Translation deadline management
- [ ] Quality review and approval
- [ ] Integration testing
- [ ] Release note translation

### Long-Term Planning

**Annual Reviews**:
- Assess supported language effectiveness
- Plan new language additions
- Review translation quality and processes
- Update tools and infrastructure

**Strategic Planning**:
- Align translation priorities with user base
- Plan for post-quantum terminology evolution
- Coordinate with ecosystem translation efforts
- Develop translation community sustainability

---
