License supplements
===================

Some Go module versions ship without an embedded license file (their tagged
module zip omits `LICENSE`), so `go-licenses` cannot find a text to attribute.
`utils/dist/gen-third-party-licenses` refuses to under-attribute such a module;
instead it looks here for a hand-supplied replacement.

Layout, keyed by the module's import path:

    <import-path>/LICENSE    verbatim upstream license text (required)
    <import-path>/SPDX       SPDX identifier, e.g. "MIT" (optional; used only
                             when go-licenses reports the type as Unknown)

Example:

    github.com/mdp/qrterminal/LICENSE   (upstream is MIT, © Mark Percival)
    github.com/mdp/qrterminal/SPDX      MIT

When adding a supplement, copy the license text verbatim from the upstream
repository at (or nearest to) the version pinned in the relevant go.mod, and
keep the copyright notice intact.
