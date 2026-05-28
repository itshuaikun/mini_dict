# Render Local Dictionary Pages

Mini Dict will render local dictionary pages with their source-provided structure through WebKitGTK instead of extracting LDOCE content into the existing simplified GTK definition model. This keeps bilingual meanings, examples, media, cross-references, and dictionary-specific layout behavior available to the user, at the cost of introducing a heavier HTML rendering dependency and reducing the value of the current JSON-shaped `LookupResult` model for local results.

**Considered Options**

- Render the source-provided dictionary page with WebKitGTK and keep Mini Dict responsible for lookup input, window behavior, and outer navigation.
- Extract LDOCE entries into native GTK widgets matching the current online API result model.
- Have the local dictionary reader return raw entry HTML and page assets, with the GTK/WebKit layer assembling the minimal document shell.

**Consequences**

The implementation should treat local dictionary content as rich dictionary pages, not as plain definition records. WebKitGTK becomes part of the local dictionary feature's runtime dependency set. The local dictionary reader should provide entry HTML fragments and assets; the GTK/WebKit layer owns the document shell, resource URL scheme, and navigation policy. Dictionary pages should not make background network requests; user-clicked external website links open in the browser. Online lookup can remain a fallback, but it should not define the primary result shape.
