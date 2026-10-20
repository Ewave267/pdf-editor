# Native performance regression baseline

`viewer-safety-tests` measures the real sandboxed worker and host event loop,
using the checked-in 1000-page fixture and AcroForm controls fixture. The normal
`viewer-safety` CTest runs these checks, including existing memory/close guards.

Run the focused checks after building `viewer-safety-tests`:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build-viewer/viewer-safety-tests largeDocumentMemoryAndClose formInteractionTiming
```

The environment must allow bubblewrap to create the PDF worker sandbox. An outer
restricted sandbox can prevent worker startup; run these checks in the same
environment as the native regression suite.

Two consecutive Linux x86-64, Qt 6.10.2 runs on 2026-10-19:

| Measurement | Run 1 | Run 2 |
| --- | ---: | ---: |
| 1000-page open | 56 ms | 55 ms |
| Maximum first render, 40 pages sampled every 25 pages at width 612 | 51 ms | 51 ms |
| Maximum immediately repeated render of those pages | 0 ms | 1 ms |
| Maximum host heartbeat gap during large-document test | 27 ms | 25 ms |
| Peak combined worker RSS | 41 MiB | 41 MiB |
| Maximum completed click/select-all/type sequence, 20 form edits | 102 ms | 103 ms |
| Maximum host heartbeat gap during form edits | 11 ms | 11 ms |

Qt polling adds up to roughly 50 ms to asynchronous completion measurements;
zero means below the elapsed timer's millisecond resolution. First render means
uncached in this document session, not a cold operating-system file cache.
The heartbeat runs on the host event loop every 10 ms and records the largest
observed interval, including opening, rendering and closing the large document.

The enforced limits catch hangs and major regressions: open under 15 seconds,
each sampled render under 10 seconds, each complete form sequence under 15
seconds, and no observed host heartbeat gap of 2 seconds. Existing budgets remain
384 MiB worker RSS, 160 MiB host growth while rendering and 80 MiB after closing.
These deliberately generous bounds accommodate slower CI hosts; they are not
interactive speed promises or representative-hardware acceptance thresholds.

No production optimization was introduced: these measurements showed no
bottleneck requiring a change. The simple generated pages do not cover scanned
image-heavy files, complex production XFA layouts, GPU presentation, or desktop
input-to-paint latency. Windows, macOS and ARM measurements remain release work.
