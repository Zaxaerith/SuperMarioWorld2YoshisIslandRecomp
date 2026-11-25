# Contributing to Yoshi's Island Recomp

Thank you for helping improve the project.

## Project scope

The core goal is **local, offline native gameplay from a user-supplied Yoshi's
Island ROM using static recompilation**. Focused compatibility, audio, input,
performance, and optional presentation improvements are welcome. Online account
systems, achievement services, platform integrations, bundled copyrighted ROMs,
and broad unrelated dependency trees are outside the intended core scope.

Keep changes focused, preserve the original experience by default, and label
experimental enhancements and known limitations clearly.

## Pull requests and verification

- Explain what changes, the supported ROM revision(s), and any gameplay or
  compatibility impact.
- Include relevant automated tests and a link to their GitHub Actions result.
  A passing ROM-free test does not replace real-game validation.
- For visual or audio fixes, provide concise before/after evidence in the
  **pull request discussion**, using GitHub drag-and-drop attachments or
  externally hosted links. For larger diagnostics, attach an archive or provide
  a short excerpt and an external link.
- **Do not commit review screenshots, video/audio captures, diagnostic dumps,
  benchmark exports, or temporary verification bundles under `docs/evidence/`
  or elsewhere in the source tree.** The `docs/evidence/` path is ignored.
  Small deterministic test fixtures required by automated tests are an
  exception; place them near their tests and explain their purpose.
- Never upload ROM dumps, save data containing personal information,
  passwords, tokens, or account credentials.

Where long-term documentation needs to cite PR evidence, link to the PR or an
immutable Git commit instead of embedding large binary review attachments.
Previously accepted filter and widescreen review materials remain accessible
through the historical commits linked from `docs/DISPLAY_FILTERS.md` and
`docs/WIDESCREEN.md`.
