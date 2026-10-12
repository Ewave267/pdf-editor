# Graph Report - pdf-editor  (2026-10-12)

## Corpus Check
- 136 files · ~71,556 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 29 file(s) not represented in the graph (top: (none) 13, .xdp 5, .patch 4)

## Summary
- 1209 nodes · 2347 edges · 71 communities (33 shown, 38 thin omitted)
- Extraction: 92% EXTRACTED · 8% INFERRED · 0% AMBIGUOUS · INFERRED: 195 edges (avg confidence: 0.86)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- Build and Packaging
- Document Public Interface
- Render Worker Protocol
- XFA Architecture and Tests
- Native XFA Probe
- Page and Overlay Rendering
- Windows Worker Sandbox
- Viewer Integration Tests
- Safety Fixture Generation
- Form Input Events
- Added Content Objects
- Dynamic XFA Calculations
- Independent Reader Checks
- Selection and Clipboard
- Added Content Interface
- Viewer Safety Tests
- Document Request Lifecycle
- Native Worker Restrictions
- Viewer Application Setup
- Native Smoke Tests
- Undo and Redo
- Rendering and Form Requests
- Native Release Acceptance
- Preview Publication Tests
- XFA Probe Tests
- Sandbox Policy Tests
- Worker Request Data
- Save Script Deadlines
- Host Access Attack Fixtures
- Infinite Script Fixtures
- Memory Attack Fixtures
- Calculation Stress Fixtures
- Script Exception Fixtures
- PDF Reader Dependencies
- Export and Print
- Native File Access
- Local Document Recovery
- Malformed Form Fixtures
- Static XFA Password Fields
- XFA Font Caption Regression
- Windows Startup Logging
- Page Dimension Limits
- XFA Field Navigation
- Focused Object Lookup
- Architecture Decision Index
- Application Icon
- Blank Page Fixture
- Mixed Page Sizes
- Crop and Rotation Fixture
- Single Page Fixture
- Corrupted PDF Fixture
- Large Page Count
- Arabic Text Fixture
- Encrypted PDF Fixture

## God Nodes (most connected - your core abstractions)
1. `PdfDocument` - 156 edges
2. `AddedContent` - 85 edges
3. `PdfDocument` - 44 edges
4. `ViewerTests` - 40 edges
5. `fixture()` - 35 edges

## Surprising Connections (you probably didn't know these)
- `AcroForm text, checkbox, radio and dropdown` --conceptually_related_to--> `Native form event semantics`  [INFERRED]
  tests/pdfs/acroform/controls.pdf → docs/adr/0005-native-form-events.md
- `Linux Qt/QML PDF viewer` --references--> `PdfDocument`  [EXTRACTED]
  docs/VIEWER.md → src/pdf/PdfDocument.h
- `0001 — Isolated PDFium XFA probe before the viewer` --references--> `runXfaProbe()`  [EXTRACTED]
  docs/adr/0001-isolated-xfa-probe.md → src/pdf/XfaProbe.cpp

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Isolated native render, form and save lifecycle** — docs_viewer_persistent_render_worker, docs_adr_0005_native_form_events_native_form_state, docs_adr_0005_native_form_events_live_save_snapshot, docs_adr_0004_isolated_save_as_isolated_atomic_save, docs_adr_0001_isolated_xfa_probe_bubblewrap_isolation [INFERRED 0.95]
- **Independent bounded persistence verification** — docs_xfa_probe_exact_roundtrip, docs_xfa_probe_pdfjs_independent_reader, docs_adr_0008_form_and_signature_reliability_staged_save_verification, docs_adr_0008_form_and_signature_reliability_bounded_compatibility_claims [INFERRED 0.95]

## Communities (71 total, 38 thin omitted)

### Community 0 - "Build and Packaging"
Cohesion: 0.05
Nodes (40): 0001 — Isolated PDFium XFA probe before the viewer, WorkerRuntimeTests, main(), apply_patch(), checkout(), build_pdfium_linux_arm64.sh script, main(), run() (+32 more)

### Community 1 - "Document Public Interface"
Cohesion: 0.03
Nodes (45): PdfDocument, active_, cache_, cachedRequests_, checkpointPending_, deadline_, diagnostics_, exportOptions_ (+37 more)

### Community 2 - "Render Worker Protocol"
Cohesion: 0.07
Nodes (22): exportPages(), fieldString(), main(), PdfDocument, bytes_, caretUpdated_, currentFormPage_, document_ (+14 more)

### Community 3 - "XFA Architecture and Tests"
Cohesion: 0.06
Nodes (27): 0002 — Asynchronous viewer with an isolated render worker, 0003 — Application-owned additions above rendered PDF pages, Visual signature image additions, 0004 — Isolated Save As with an immutable source snapshot, 0005 — Native form events and live save snapshots, 0006 — Worker syscall restrictions, allocation limits and controlled failure, Historical cryptographic-signature deferral, ADR 0007: evaluate advanced capabilities after the core release gates (+19 more)

### Community 4 - "Native XFA Probe"
Cohesion: 0.06
Nodes (26): path, Host, caret, changed, changePage, deniedRequests, javascript, lookupPage (+18 more)

### Community 5 - "Page and Overlay Rendering"
Cohesion: 0.06
Nodes (11): QPainter, width, PdfDocument::saveAs(), AddedOverlay, page_, selection_, PdfPageItem, debounce_ (+3 more)

### Community 6 - "Windows Worker Sandbox"
Cohesion: 0.06
Nodes (23): Attributes, list, storage, Desktop, value, grantRead(), Handle, value (+15 more)

### Community 7 - "Viewer Integration Tests"
Cohesion: 0.09
Nodes (4): fixture(), item(), ViewerTests, waitForFrame()

### Community 8 - "Safety Fixture Generation"
Cohesion: 0.11
Nodes (13): Probe fixture corpus, BSD-licensed pinned upstream PDFium samples, WorkerSafetyTests, generate(), acroform_controls(), generate(), many_pages_pdf(), pdf() (+5 more)

### Community 10 - "Form Input Events"
Cohesion: 0.12
Nodes (3): FormInput, FormInput::FormInput(), page_

### Community 11 - "Added Content Objects"
Cohesion: 0.08
Nodes (15): Object, bold, color, fontFamily, fontSize, id, image, italic (+7 more)

### Community 12 - "Dynamic XFA Calculations"
Cohesion: 0.08
Nodes (22): TextField1 and TextField11 in XFA template 3.3, XFA packet-array representation, JavaScript calculated.rawValue derives JS: prefix from input.rawValue, Text, checkbox, exclusive radio group and dropdown controls, XFA datasets: input=original, check=0, choice=A, dropdown=Alpha, XFA dynamicRender required, Initialize script requests https://example.invalid/blocked, JavaScript calculated.rawValue derives JS: prefix from input.rawValue (+14 more)

### Community 13 - "Independent Reader Checks"
Cohesion: 0.14
Nodes (12): [directory, filename, kind, checked = "1", radio = "B", dropdown = "Beta"], task, [directory, filename], task, [directory, filename, kind], task, [directory, filename, kind], task (+4 more)

### Community 15 - "Added Content Interface"
Cohesion: 0.10
Nodes (11): AddedContent, editRecorded_, editRects_, editRedo_, editState_, editUndo_, nextId_, nextRevision_ (+3 more)

### Community 16 - "Viewer Safety Tests"
Cohesion: 0.19
Nodes (13): fieldScriptDeadline(), fixture(), formQueueIsBounded(), largeDocumentMemoryAndClose(), malformedDocuments(), oversizedInput(), processTree(), rendererCrashRetainsAdditions() (+5 more)

### Community 18 - "Native Worker Restrictions"
Cohesion: 0.13
Nodes (3): installWorkerPolicy(), limit(), applyMacWorkerSandbox()

### Community 21 - "Native Smoke Tests"
Cohesion: 0.28
Nodes (6): event(), main(), ready(), render(), require(), wait()

### Community 22 - "Undo and Redo"
Cohesion: 0.15
Nodes (5): State, objects, revision, selected, selection

### Community 24 - "Native Release Acceptance"
Cohesion: 0.14
Nodes (3): PATH, build_native_linux.sh script, setup_native_linux.sh script

### Community 30 - "Sandbox Policy Tests"
Cohesion: 0.22
Nodes (3): installWorkerPolicy(), check(), main()

### Community 33 - "Worker Request Data"
Cohesion: 0.20
Nodes (6): Request, command, id, listeners, page, queuedMs

### Community 34 - "Save Script Deadlines"
Cohesion: 0.22
Nodes (8): Dynamic XFA: client render policy and dynamicRender required, XFA input exit event contains while(true){}; initialize event requests blocked example.invalid URL, Calculated rawValue is JS: prefix plus input.rawValue, Dataset initial values: input original, check 0, choice A, dropdown Alpha, XFA XML packet referenced from AcroForm, AcroForm controls: Input original, checkbox Off, radio Choice A, Dropdown Alpha, Document-level JavaScript name-tree action, Catalog additional action WS invokes JavaScript containing while(true){}

### Community 35 - "Host Access Attack Fixtures"
Cohesion: 0.22
Nodes (8): AcroForm controls: Input original, checkbox Off, radio Choice A, Dropdown Alpha, Document-level JavaScript name-tree action, JavaScript probes runtime globals, file reads, file launch, saveAs, submitForm and records audit in Input, Dynamic XFA: client render policy and dynamicRender required, Initialize JavaScript probes runtime globals, file reads, file launch and xfa.host.gotoURL; stores audit in rawValue, Calculated rawValue is JS: prefix plus input.rawValue, Dataset initial values: input original, check 0, choice A, dropdown Alpha, XFA XML packet referenced from AcroForm

### Community 36 - "Infinite Script Fixtures"
Cohesion: 0.22
Nodes (8): AcroForm controls: Input original, checkbox Off, radio Choice A, Dropdown Alpha, Document-level JavaScript name-tree action, Document-level JavaScript action contains while(true){}, Dynamic XFA: client render policy and dynamicRender required, XFA initialize JavaScript contains while(true){}, Calculated rawValue is JS: prefix plus input.rawValue, Dataset initial values: input original, check 0, choice A, dropdown Alpha, XFA XML packet referenced from AcroForm

### Community 37 - "Memory Attack Fixtures"
Cohesion: 0.22
Nodes (8): AcroForm controls: Input original, checkbox Off, radio Choice A, Dropdown Alpha, Document-level JavaScript name-tree action, JavaScript attempts Uint8Array(1024*1024*1024) and records unavailable, allowed, or blocked in Input, Dynamic XFA: client render policy and dynamicRender required, Calculated rawValue is JS: prefix plus input.rawValue, Initialize JavaScript attempts Uint8Array(1024*1024*1024) and records unavailable, allowed, or blocked in rawValue, Dataset initial values: input original, check 0, choice A, dropdown Alpha, XFA XML packet referenced from AcroForm

### Community 38 - "Calculation Stress Fixtures"
Cohesion: 0.22
Nodes (8): AcroForm controls: Input original, checkbox Off, radio Choice A, Dropdown Alpha, Document JavaScript sums indices in 100000-iteration loop and writes sum to Input, Document-level JavaScript name-tree action, Initialize JavaScript sums indices in 100000-iteration loop and writes sum to rawValue, Dynamic XFA: client render policy and dynamicRender required, Calculated rawValue is JS: prefix plus input.rawValue, Dataset initial values: input original, check 0, choice A, dropdown Alpha, XFA XML packet referenced from AcroForm

### Community 39 - "Script Exception Fixtures"
Cohesion: 0.22
Nodes (8): AcroForm controls: Input original, checkbox Off, radio Choice A, Dropdown Alpha, Document-level JavaScript name-tree action, JavaScript sets Input to ran-before-error then throws Error synthetic failure, Dynamic XFA: client render policy and dynamicRender required, Calculated rawValue is JS: prefix plus input.rawValue, Initialize JavaScript sets rawValue to ran-before-error then throws Error synthetic failure, Dataset initial values: input original, check 0, choice A, dropdown Alpha, XFA XML packet referenced from AcroForm

### Community 40 - "PDF Reader Dependencies"
Cohesion: 0.22
Nodes (8): pdfjs-dist, dependencies, pdfjs-dist, engines, node, name, private, type

### Community 41 - "Export and Print"
Cohesion: 0.22
Nodes (4): PdfDocument::exportPages(), PdfDocument::finishSave(), PdfDocument::print(), PdfDocument::printToPdf()

### Community 49 - "Malformed Form Fixtures"
Cohesion: 0.33
Nodes (4): Truncated XDP XML: template and broken elements remain unclosed, XFA XML packet referenced from AcroForm, AcroForm controls: Input original, checkbox Off, radio Choice A, Dropdown Alpha, Radio field object 8 has a self-reference in its Kids array

### Community 52 - "Static XFA Password Fields"
Cohesion: 0.40
Nodes (4): Password fields with top, middle and bottom anchor positions, XFA passwordEdit fields at 90, 180 and 270 degrees, XFA dynamicRender forbidden, XFA packet-array representation

### Community 53 - "XFA Font Caption Regression"
Cohesion: 0.40
Nodes (4): Arial requested caption versus Liberation Sans reference, XFA dynamicRender required, French rich-text captions mixing bold and normal spans, Round checkButton fields requested and reference

### Community 57 - "XFA Field Navigation"
Cohesion: 0.50
Nodes (3): XFA dynamicRender required, first and second text fields on separate subforms, secondPage breakBefore starts new pageArea

## Knowledge Gaps
- **198 isolated node(s):** `value`, `value`, `value`, `value`, `value` (+193 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 506 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **38 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `PdfDocument` connect `Document Public Interface` to `Worker Request Data`, `XFA Architecture and Tests`, `Page and Overlay Rendering`, `Document Save Implementation`, `Export and Print`, `Local Document Recovery`, `Save Preservation Guarantees`, `Added Content Interface`, `Document Request Lifecycle`, `Rendering and Form Requests`, `Focused Object Lookup`, `Form and Desktop Experience`, `Document State Dependencies`, `Added Content Documentation`?**
  _High betweenness centrality (0.194) - this node is a cross-community bridge._
- **What connects `value`, `value`, `value` to the rest of the system?**
  _198 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Build and Packaging` be split into smaller, more focused modules?**
  _Cohesion score 0.05012313486889758 - nodes in this community are weakly interconnected._

## Extraction limits and token accounting

- Semantic extraction ran through this session without a separate API key. Token usage and monetary cost are unavailable from the agent interface; displayed zero counters are placeholders, not a zero-cost claim. AST extraction is deterministic and requires no LLM tokens.
- Graph health warning: 419 dangling-endpoint references and 67 collapsed undirected relationships. See GRAPH_HEALTH.md for raw diagnostics. External/library reference placeholders are included by the builder; they are not verified project declarations.
- Partial C++ parsing: tests/test_viewer.cpp and tests/test_viewer_safety.cpp. No symbols extracted from src/pdf/WorkerPolicy.h or tools/setup_msvc.ps1.
- PDF fixtures were inspected using text extraction and literal structures; scripts were not executed, and full form/layout behavior was not tested. Malformed inputs and encrypted content without a password were recorded explicitly. The SVG icon was rendered and visually inspected.
- Detector-unclassified files (including QML, XDP, patches and some packaging/configuration files) have no dedicated extraction pass. Build/dependency outputs and private documents were excluded. This is a project map, not exhaustive runtime or security validation.
- graphify-out/ was written into the working tree and currently appears in git status; it can be ignored or selected artifacts can be committed.
