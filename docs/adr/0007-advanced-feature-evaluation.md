# ADR 0007: evaluate advanced capabilities after the core release gates

Status: accepted scope decision, 2026-10-10.

The requested scope is core roadmap phases 1–7, with evaluation of phase 8.
Advanced capabilities are not implemented by this decision.

| Capability | Decision | Requirements before implementation |
| --- | --- | --- |
| OCR | Defer | A demonstrated scanned-document use case, bounded isolated OCR engine, language packs, licensing and searchable-text alignment tests. |
| Form creation | Defer | Field authoring model, PDFium write APIs, explicit export/interoperability tests and editable project storage. |
| Cryptographic signatures | Defer | Certificate/key storage, signing library, incremental saves, independent signature validation and platform trust integration. Visual signature images are already supported. |
| Redaction | Defer | Permanent removal of underlying text/images/metadata and independent recovery-attempt tests. An opaque rectangle is not redaction. |
| PDF/A | Defer | A chosen conformance level, color/font metadata policy and independent standards validation. |
| Accessibility improvements | Incremental | Native screen-reader and keyboard testing first; tagged-PDF creation/repair requires a separate structural editing design. |
| Plugins | Defer | Concrete extension use cases, stable API, permission model, signing/update policy and isolation from PDF parsing. |

Prioritize compatibility, recovery, source preservation and the production
release gates before adding dependencies or promising document conformance.
A feature proposal should include its user need, threat model, export semantics,
license obligations and a regression plan before implementation begins.
