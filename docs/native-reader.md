# Native FB3 reader (v0.6.14)

The standard LitRes library remains in WebEngine. A main-frame HTTPS navigation to `/static/reader/text/index.html` opens `NativeReader`; its menu can explicitly open the web version instead.

`ReaderTransport` uses a separate, blank HTTPS page in the existing authenticated profile. It fetches metadata, the TOC and only the current/next listed FB3 part. Text is not executed as JavaScript. `Fb3Data` parses a limited, bounded data-only JSON5 subset. `Fb3Layout` preserves text anchors and UTF-16 offsets (excluding soft hyphens), uses native QTextDocument line layout, and clips only complete lines. Page turns overlap one line within a part; no paragraphs are discarded. Font size is remembered. Raster illustrations use native image objects with their own XPointer anchors; they are scaled to fit a complete page and preserve their aspect ratio. The transport fetches only image filenames listed in the FB3 nodes, using the same authenticated cookies. Chromium decodes raster data once and normalizes it to bounded PNG (maximum 758×1024) so the firmware does not need a JPEG plugin. Native image decoding rejects invalid or oversized data. The reader preloads pictures near the viewport, releases distant document image resources and bounds its decoded-image cache to 8 MiB. Left/right arrows have no reading action; physical paging keys and up/down control text, OK opens the menu, Back returns to the library.

Progress uses the observed `/foundation/api/users/me/arts/{art}/fb3/autobookmark` API. On opening, GET restores the server pointer. Local records under `native-reader/{user}/{art}.json` contain only bookmark state, not credentials or book text. Writes are debounced and serialized, use the last server version plus one, and retain pending changes during network failures. A changed server position or HTTP 409 blocks automatic overwrite. The user chooses the local or remote position. Acknowledgements cannot clear a later local edit. Explicit logout removes native reader state before removing the authenticated profile.

Last-book history is written only after a visible book part opens successfully. Each account has its own `native-reader/{user}/last-book.json`, containing only the book ID/title and opening time. Pressing LitRes verifies the active user before showing a plain-text Yes/No prompt. Yes opens a fresh reader, re-fetching metadata, TOC and the server bookmark; No opens My Books. Existing pending local progress is retained and the normal conflict policy still applies. The first book opened after upgrading establishes the history; there is no guess based on older bookmark file timestamps.

Limitations: this implementation supports normal FB3 text and raster illustrations. EPUB, DRM and unsupported resource formats use the explicit web fallback. No decryption or access-control bypass is implemented. TOC/part reads must succeed using the user's existing session. Only bookmark state persists across app restarts; offline book storage, footnote navigation, cloud conflict behavior on a second physical device and e-ink timings remain future/device checks.

## Validation

- ARM EABI5 soft-float app compiled using the cached builder and existing firmware Qt libraries (`--pull=never`).
- `tools/tests/native-reader.cpp`: strict data parsing, parent/inline XPointer round trips, complete line coverage for nine width/font combinations, offline state, conflicting versions, edits during save and ambiguous network results. Optional private chunk argument validates real text anchors without committing book content.
- Isolated native Qt test opened an authorized real book using GET/OPTIONS only, restored the current remote pointer, and produced a native rendering. A request interceptor blocked all network writes. The same transport serialized/acknowledged a POST through a mocked `fetch`, with zero network POST requests.
- `reader-images-history.cpp`: portrait, landscape and square illustrations retain proportions and fit in 24 viewport/font layouts, full line coverage with images, stable text/image anchors, durable history, validation and account isolation.
- `reader-resume-ui.cpp`: real MainWindow Yes, No and other-account paths against a synthetic API; fresh cloud position supersedes an old synchronized local record; JPEG passes through the real Chromium binary transport and PNG normalization, then native rendering. Network writes are blocked. Browser teardown is checked after the reader closes.
- Tests do not demonstrate actual PB617 e-ink latency or a live cross-device write/read cycle. Those require reading on the device; the real cloud bookmark was not moved by test traffic.

A generic Linux Qt test build:

```
g++ -std=c++11 -fPIC tools/tests/native-reader.cpp -Isrc $(pkg-config --cflags --libs Qt5Widgets Qt5WebEngineWidgets) -o /tmp/native-reader-test
QT_QPA_PLATFORM=offscreen /tmp/native-reader-test
```

No cookies, private signing keys or book content belong in repository fixtures.

Image/history tests use only generated artwork and synthetic book data. Compile `tools/tests/reader-images-history.cpp` with Qt5Widgets and `-Isrc`. The UI test includes the application and writes its fixed PocketBook data paths; run it **only in a disposable container**, never against an actual mounted reader. Its desktop SDK stubs are in `tools/tests/stubs/inkview.h`. Link Qt5Widgets, Qt5WebEngineWidgets, Qt5Test and the two existing Monocypher C objects. Arguments `yes`, `no`, `other` exercise each branch. No account credentials or copyrighted fixtures are required.
