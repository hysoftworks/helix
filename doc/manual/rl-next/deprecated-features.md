---
synopsis: 'more deprecated features'
issues: []
cls: []
category: Breaking Changes
credits: [piegames, horrors]
---

This release cycle features a new batch of deprecated (anti-)features.
You can opt in into the old behavior with `--extra-deprecated-features` or any equivalent configuration option.

- `cr-line-endings`: Current handling of CR (`\r`) or CRLF (`\r\n`) line endings in Nix is inconsistent and broken, and will lead to unexpected evaluation results with certain strings. Given that fixing the semantics might silently alter the evaluation result of derivations, the only option at the moment is to disallow them altogether. More proper support for CRLF is planned to be added back again in the future. Until then, all files must use `\n` exclusively.
- `nul-bytes`: Currently the Nix grammar allows NUL bytes (`\0`) in strings, and thus indirectly also in identifiers. Unfortunately, several core parts of the code base still work with NUL-terminated strings and cannot easily be migrated. Also note that it is still possible to introduce NUL bytes and thus problematic behavior via other means, those are tracked separately.
