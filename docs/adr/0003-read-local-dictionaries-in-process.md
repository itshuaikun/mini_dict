# Read Local Dictionaries In Process

Mini Dict will read local dictionary files itself instead of requiring an external dictionary application, daemon, or user-managed lookup service. The Local Dictionary Reader may use an in-process MDX/MDD library, including a restrictive-license library when that best preserves local lookup quality for personal use, and it may be implemented as a Rust component exposed through a narrow C ABI. Lookup must remain a Mini Dict capability. This keeps Word Lookup as a self-contained application capability and avoids making the Lookup Window depend on another product's installation, configuration, lifecycle, and error states, at the cost of adding MDX/MDD reading responsibility to Mini Dict and revisiting licensing if the application is ever redistributed.

**Considered Options**

- Read the Local Dictionary Source directly inside Mini Dict.
- Use an in-process MDX/MDD library as part of Mini Dict, including through a narrow C ABI wrapper when the library is not written in C.
- Delegate local lookup to an external dictionary program or service.

**Consequences**

Mini Dict should report local dictionary setup and lookup errors in its own terms. External dictionary applications may remain useful for comparison or debugging, but they are not part of the runtime contract. The GTK application should depend on a small reader boundary rather than on MDX/MDD internals. If Mini Dict is later distributed beyond personal local use, MDX/MDD library licensing must be reviewed again.
