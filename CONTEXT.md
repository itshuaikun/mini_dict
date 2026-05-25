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
The dictionary-style answer returned for one Lookup Query. A complete Lookup Result is shown as a Full Dictionary Page and includes British phonetic transcription, American phonetic transcription, English meanings, and native English example sentences; Chinese meanings are optional.
_Avoid_: Translation result, AI answer

**Full Dictionary Page**:
A scrollable Lookup Result that presents all available parts of speech, all available English meanings, native example sentences when available, and pronunciation audio when available. It does not imply synonyms, antonyms, etymology, or inflections unless those are explicitly added later.
_Avoid_: Quick summary, short card

**Native Example Sentence**:
An English sentence that shows natural usage of a word or short phrase. It should read like authentic English, not a literal translation from Chinese. It may belong to a specific meaning or to the Lookup Result as a whole.
_Avoid_: Translated example, machine-made example

**Pronunciation Audio**:
A playable audio recording for a Lookup Query's pronunciation when the dictionary source provides one, usually associated with a British or American pronunciation. It is separate from phonetic transcription and may be unavailable for some Lookup Results.
_Avoid_: Required pronunciation, generated voice

**Lookup Window**:
The small application window where the user enters a Lookup Query and reads the Lookup Result. It is the main user-facing surface of the product.
_Avoid_: Translator window, popup translation panel

**Wake Shortcut**:
A global keyboard shortcut that toggles the Lookup Window between visible and hidden. It does not read selected text, use the clipboard, or submit a Lookup Query by itself.
_Avoid_: Selection shortcut, clipboard shortcut, translate hotkey

**Cached Lookup Result**:
A previously successful Lookup Result saved locally so the same Lookup Query can be shown faster or when the network is unavailable. It is not a user-managed vocabulary list or lookup history.
_Avoid_: Wordbook, favorites, history

## Example Dialogue

Dev: Should the main window say "Translate"?

Domain expert: No. This product performs Word Lookup, so "Look up" is the better command.

Dev: Can the user enter "I looked up the word yesterday"?

Domain expert: No. That is a sentence. "look up" is a Short Phrase and can be a Lookup Query.

Dev: Is a Chinese paraphrase alone enough for a Lookup Result?

Domain expert: No. A complete Lookup Result needs British and American phonetic transcription, English meanings, and Native Example Sentences. Chinese meanings can be added when a reliable source is available.

Dev: Does a Lookup Result always need playable pronunciation?

Domain expert: No. Pronunciation Audio is shown when the dictionary source provides it, and it is separate from British and American phonetic transcription.

Dev: Should the Wake Shortcut look up the currently selected word?

Domain expert: No. It only shows or hides the Lookup Window. The user enters the Lookup Query in the window.

Dev: Can users organize cached words into study lists?

Domain expert: No. A Cached Lookup Result only supports faster repeat lookup and weak offline viewing.
