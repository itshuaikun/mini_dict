# Build A Local Chinese Reverse Lookup Index

Mini Dict will support Chinese-to-English Word Lookup by building a local reverse index from the configured LDOCE 5++ V2.15 Local Dictionary Source. Chinese input is not sent to an online translation service, and it does not become sentence translation. The feature remains a dictionary lookup path: Chinese terms find candidate English entries, and opening a candidate shows the existing Full Dictionary Page for that English entry.

The first version will accept only pure Chinese Lookup Queries of 1 to 8 Han characters. It will reject punctuation, whitespace, digits, Latin letters, pinyin, mixed Chinese-English input, and sentence-like input. English word and short phrase lookup continues to use the existing Local Dictionary Reader path.

Chinese queries show a keyboard-navigable candidate list before opening an entry. The list contains at most 15 candidates and supports Up/Down selection, Enter to open the selected candidate, and mouse click to open. Each candidate should show the English entry key, part of speech when cheaply available, and the matched Chinese meaning snippet. Selecting a candidate replaces the input text with the English key and opens that key through the existing local lookup path. The first version will not provide a back action from the Full Dictionary Page to the candidate list.

The reverse index will be built lazily on the first Chinese query, persisted under Mini Dict's cache area, and reused while the source `.mdx` path, size, and modification time match the indexed metadata. Index building must run in the background so English lookup remains usable. When the build completes, the app should refresh results only if the active query is still the same Chinese query. A CLI command such as `--rebuild-chinese-index` will force rebuilding for troubleshooting, but the Lookup Window will not add a rebuild button.

Index construction will scan MDX entries through the existing Rust reader boundary and extract Chinese text from each entry's HTML after filtering scripts, styles, tags, and obvious noise. The first version will not follow `@@@LINK=` redirects during indexing; opening a chosen candidate still uses the existing lookup behavior, which already follows redirects. Search results will be deduplicated by English key, preserving the highest-scoring and most explanatory snippet.

Ranking will stay simple and deterministic: prefer exact Chinese phrase matches, earlier matches, main/basic word entries, shorter single-word English keys, and then stable lexical ordering. Phrase entries remain eligible, but equal-quality base word entries rank ahead of phrase, compound, or derivative-looking keys. The first version will not parse LDOCE frequency labels or build an embedding/semantic index.

If Chinese reverse lookup has no candidates, Mini Dict should show an empty result state and should not offer Online Lookup Fallback. If the Chinese index cannot be built or queried, the app should report that Chinese reverse lookup is unavailable while leaving English lookup unaffected.

**Considered Options**

- Build a local reverse index from the configured LDOCE content.
- Send Chinese input to an online translation or dictionary service.
- Add a separate Chinese-English dictionary source as the primary reverse lookup authority.
- Scan all MDX HTML synchronously on every Chinese query.
- Use embedding or pinyin search in the first version.

**Consequences**

Chinese reverse lookup remains aligned with Mini Dict's Local Dictionary Source and Word Lookup boundaries. The implementation adds an index-building step and candidate-list UI state, but avoids a translation product surface, network dependency, and heavyweight semantic infrastructure. Search quality can improve later by extracting more precise LDOCE meaning regions or using frequency markers, without changing the first-version contract.
