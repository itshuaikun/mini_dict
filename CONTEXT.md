# Mini Dict

This context describes the vocabulary for a small dictionary lookup application. It exists to keep product language precise as the application grows.

## Language

**Word Lookup**:
A request for information about an English word or short English phrase, returning dictionary-style details such as pronunciation, meanings, and example sentences. It does not mean sentence translation, paragraph translation, OCR, or multi-language translation.
_Avoid_: Translation, translator, sentence translation

**Lookup Query**:
The text the user submits for a Word Lookup. A Lookup Query is either one English word or a common short English phrase, not a full sentence.
_Avoid_: Source text, translation text, paragraph

**Short Phrase**:
A common multi-word English expression that behaves like a dictionary entry, such as a phrasal verb or idiom. It is eligible for Word Lookup only when it is not a full sentence.
_Avoid_: Sentence, paragraph

**Lookup Result**:
The dictionary-style answer returned for one Lookup Query. A complete Lookup Result is shown as a Full Dictionary Page and includes British phonetic transcription, American phonetic transcription, English meanings, Chinese meanings, and native English example sentences.
_Avoid_: Translation result, AI answer

**Chinese Meaning**:
A Chinese explanation attached to an English word, phrase, or meaning inside a Lookup Result. It complements English meanings; it is not a standalone translation of arbitrary user text.
_Avoid_: Sentence translation, Chinese answer, machine translation

**Full Dictionary Page**:
A scrollable Lookup Result that preserves the Local Dictionary Source's available dictionary structure, including parts of speech, English meanings, Chinese meanings, native example sentences, and pronunciation audio when available. It does not imply sentence translation or generated explanations outside the dictionary source.
_Avoid_: Quick summary, short card

**Main Dictionary Entry**:
The most exact Local Dictionary Source entry for a Lookup Query. It may contain the source's own phrases, word families, and cross-references, but it does not mean Mini Dict has merged several candidate entries into one result.
_Avoid_: Search results list, merged candidates, related-word browser

**Native Example Sentence**:
An English sentence that shows natural usage of a word or short phrase. It should read like authentic English, not a literal translation from Chinese. It may belong to a specific meaning or to the Lookup Result as a whole.
_Avoid_: Translated example, machine-made example

**Pronunciation Audio**:
A playable audio recording for a Lookup Query's pronunciation when the dictionary source provides one, usually associated with a British or American pronunciation. It may be launched from the Full Dictionary Page or from application controls, and it is separate from phonetic transcription.
_Avoid_: Required pronunciation, generated voice

**Dictionary Page Asset**:
A local asset used by a Full Dictionary Page, such as stylesheet, script, pronunciation audio, or image content. Stylesheets, scripts, and Pronunciation Audio are part of the core page experience; images are useful supporting content but may be unavailable without invalidating the Lookup Result.
_Avoid_: Cache file, downloaded media, required image

**Dictionary Page Link**:
A link inside a Full Dictionary Page. Links to entries or assets in the same Local Dictionary Source stay inside the Lookup Window; links to external websites open in the user's browser.
_Avoid_: Search result, hidden network lookup, ignored link

**Local Dictionary Source**:
The locally available LDOCE 5++ V2.15 dictionary content used as the primary authority for Word Lookup. It may provide English meanings, Chinese meanings, native example sentences, pronunciation details, and media for a Lookup Result.
_Avoid_: Generic MDict support, offline fallback, backup API, translation engine

**Local Dictionary Reader**:
The application capability that reads a Local Dictionary Source directly for Word Lookup. It is part of Mini Dict, not a separate desktop dictionary application or user-managed background service.
_Avoid_: External dictionary app, helper service, GoldenDict dependency

**Local Dictionary Directory**:
A user-selectable local location that contains a Local Dictionary Source and its related media or page assets. It is part of the user's dictionary setup, not a lookup history or cache location.
_Avoid_: Cache directory, bundled data, download folder

**Dictionary Setup Issue**:
A state where the Local Dictionary Source cannot be used because its Local Dictionary Directory is missing, unreadable, or not selected. It is distinct from a Lookup Query that has no matching Lookup Result.
_Avoid_: No entry found, lookup failure, network fallback

**No Local Entry**:
A Word Lookup outcome where the Local Dictionary Source was usable and the Lookup Query was valid, but no Main Dictionary Entry matched it. It is eligible for an Online Lookup Fallback action.
_Avoid_: Dictionary setup issue, parse error, broken dictionary

**Online Lookup Fallback**:
An explicitly chosen secondary Word Lookup path that uses an online dictionary source when the Local Dictionary Source has no matching Lookup Result or the user asks for online lookup. It may return a simplified result, is not the primary source, and does not hide Dictionary Setup Issues.
_Avoid_: Primary dictionary, silent fallback, network-only mode

**Lookup Window**:
The small application window where the user enters a Lookup Query and reads the Lookup Result. It is the main user-facing surface of the product.
_Avoid_: Translator window, popup translation panel

**Wake Shortcut**:
A global keyboard shortcut that toggles the Lookup Window between visible and hidden. It does not read selected text, use the clipboard, or submit a Lookup Query by itself.
_Avoid_: Selection shortcut, clipboard shortcut, translate hotkey

**Cached Lookup Result**:
A previously successful Online Lookup Fallback result saved locally so the same Lookup Query can be shown faster or when the network is unavailable. It does not duplicate Local Dictionary Source page content and is not a user-managed vocabulary list or lookup history.
_Avoid_: Wordbook, favorites, history

## Example Dialogue

Dev: Should the main window say "Translate"?

Domain expert: No. This product performs Word Lookup, so "Look up" is the better command.

Dev: Can the user enter "I looked up the word yesterday"?

Domain expert: No. That is a sentence. "look up" is a Short Phrase and can be a Lookup Query.

Dev: Is a Chinese paraphrase alone enough for a Lookup Result?

Domain expert: No. A complete Lookup Result needs British and American phonetic transcription, English meanings, Chinese meanings, and Native Example Sentences.

Dev: Does a Lookup Result always need playable pronunciation?

Domain expert: No. Pronunciation Audio is shown when the dictionary source provides it, and it is separate from British and American phonetic transcription.

Dev: Does a missing image make a Lookup Result invalid?

Domain expert: No. Images are Dictionary Page Assets, but only stylesheets, scripts, and available Pronunciation Audio are core to the page experience.

Dev: What should happen when the user clicks a Dictionary Page Link?

Domain expert: Same-dictionary links stay in the Lookup Window. External website links open in the user's browser.

Dev: Should Mini Dict prefer the online dictionary when both online and local content exist?

Domain expert: No. The LDOCE 5++ V2.15 Local Dictionary Source is the primary authority for Word Lookup, with online lookup only as a fallback.

Dev: Does the Local Dictionary Source have to be built into the application?

Domain expert: No. It lives in a Local Dictionary Directory selected or discovered as part of the user's dictionary setup.

Dev: Can Mini Dict delegate local lookup to another dictionary application?

Domain expert: No. Mini Dict owns the Local Dictionary Reader so Word Lookup does not depend on an external desktop dictionary app or background service.

Dev: If the Local Dictionary Directory is missing, should Mini Dict silently show online results?

Domain expert: No. That is a Dictionary Setup Issue, not an empty Lookup Result.

Dev: When should Mini Dict offer Online Lookup Fallback after local lookup?

Domain expert: Offer it for No Local Entry, not for Dictionary Setup Issues or broken local dictionary content.

Dev: Is online lookup still part of Mini Dict?

Domain expert: Yes, but only as an Online Lookup Fallback. It should not define the primary Lookup Result shape or hide Dictionary Setup Issues.

Dev: Can Mini Dict reduce a Full Dictionary Page to a few extracted definition rows?

Domain expert: No. A Full Dictionary Page should preserve the Local Dictionary Source's available structure instead of becoming a quick summary.

Dev: If several local entries resemble the Lookup Query, should Mini Dict merge them?

Domain expert: No. It should show the Main Dictionary Entry and rely on the Local Dictionary Source's own cross-references.

Dev: Should the Wake Shortcut look up the currently selected word?

Domain expert: No. It only shows or hides the Lookup Window. The user enters the Lookup Query in the window.

Dev: Can users organize cached words into study lists?

Domain expert: No. A Cached Lookup Result only supports faster repeat online fallback lookup and weak offline viewing.
