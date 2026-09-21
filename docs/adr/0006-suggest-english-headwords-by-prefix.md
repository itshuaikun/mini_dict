# Show English Prefix Suggestions While Typing

Mini Dict will show a live candidate list while the user types an English Lookup Query. A candidate is an English headword whose entry key starts with the typed text, so the user can see and choose the intended Main Dictionary Entry before submitting the query. The list is a navigation aid on the way to a Word Lookup; it is not a Lookup Result, a merged result list, or a translation surface.

Candidates come from the local entry index that already backs Chinese reverse lookup. That index stores the headword, part of speech, and extracted Chinese meaning for every LDOCE entry it indexed, so prefix matching needs no new persistence, no network access, and no additional scan of the `.mdx` source. Matching is case-insensitive and uses a `lower(entry_key)` expression index so a range scan stays fast while the user types.

The lookup window refreshes the list after a short typing pause, shows at most 15 candidates, and displays the headword, part of speech, and a short Chinese meaning snippet for each candidate. Up and Down move the selection; Enter opens the highlighted candidate once the user has moved the selection, and otherwise keeps the existing exact Lookup Query behaviour so a complete headword such as `rose` still opens its own entry even when it is absent from the index; clicking a row opens it; clearing the input returns the window to its empty state. Chinese input keeps its existing behaviour, where Enter opens the first candidate. Refreshing the list must not move the caret or select the input text, so the user can keep typing a whole word without interruption.

The index is built lazily. If it is missing or stale when the user starts typing English, Mini Dict warms it up in the background and refreshes the list for the current input when the build finishes. Until then the list is simply absent; exact lookup, the Full Dictionary Page, and online fallback are unaffected.

Ranking stays deterministic and explainable: an exact headword match first, then single-word headwords before phrases, then shorter keys, then lexical order. The first version will not use LDOCE frequency labels, word-family data, fuzzy matching, or online suggestion services.

**Considered Options**

- Reuse the local Chinese reverse lookup index for English headword prefixes.
- Build a separate persistent headword index from every MDX key.
- Build an in-memory headword list at startup through a new Rust keys-only reader API.
- Query an online suggestion or autocomplete service while typing.
- Rank suggestions by word frequency.

**Consequences**

The feature adds no new dictionary source, no new cache database, and no Rust FFI change, and it reuses the candidate-list UI that Chinese reverse lookup already has. The tradeoff is coverage: the index contains the 54,044 LDOCE entries that have an extracted Chinese meaning, so entries without one, such as some proper nouns and multiword encyclopedia titles, are not suggested. Exact lookup through the Local Dictionary Reader still finds those entries, and extending the index to every MDX key remains a later option.
