# Keep Local Dictionary Content Separate From App Install

Mini Dict may keep the LDOCE 5++ V2.15 dictionary directory in this personal repository for local development, but the application will not copy or install that dictionary content as app-owned data. Users still provide a Local Dictionary Directory through configuration or automatic discovery, which keeps the application runtime boundary separate from large third-party dictionary content.

**Considered Options**

- Keep the dictionary directory in the personal development repository while still requiring the app to use a configured or discovered Local Dictionary Directory.
- Require users to provide or point to their own Local Dictionary Directory outside the repository.
- Copy the local dictionary directory into Mini Dict's application data during install or setup.
- Bundle the dictionary content with the application package.

**Consequences**

Installing Mini Dict and preparing the Local Dictionary Directory are separate steps. The application should make missing or unreadable dictionary content a clear Dictionary Setup Issue instead of silently falling back to online lookup. If the repository is ever redistributed beyond personal local use, the included dictionary content must be reviewed separately from the application code.
