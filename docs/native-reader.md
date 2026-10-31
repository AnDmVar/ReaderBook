# Native FB3 reader (v0.6.13)

The standard LitRes library remains in WebEngine. A main-frame HTTPS navigation to `/static/reader/text/index.html` opens `NativeReader`; its menu can explicitly open the web version instead.

`ReaderTransport` uses a separate, blank HTTPS page in the existing authenticated profile. It fetches metadata, the TOC and only the current/next listed FB3 part. Text is not executed as JavaScript. `Fb3Data` parses a limited, bounded data-only JSON5 subset. `Fb3Layout` preserves text anchors and UTF-16 offsets (excluding soft hyphens), uses native QTextDocument line layout, and clips only complete lines. Page turns overlap one line within a part; no paragraphs are discarded. Font size is remembered. Left/right arrows have no reading action; physical paging keys and up/down control text, OK opens the menu, Back returns to the library.

Progress uses the observed `/foundation/api/users/me/arts/{art}/fb3/autobookmark` API. On opening, GET restores the server pointer. Local records under `native-reader/{user}/{art}.json` contain only bookmark state, not credentials or book text. Writes are debounced and serialized, use the last server version plus one, and retain pending changes during network failures. A changed server position or HTTP 409 blocks automatic overwrite. The user chooses the local or remote position. Acknowledgements cannot clear a later local edit. Explicit logout removes native reader state before removing the authenticated profile.

Limitations: first implementation supports normal FB3 text. Illustrations are marked with a web-reader option; EPUB, DRM and unsupported resource formats use the explicit web fallback. No decryption or access-control bypass is implemented. TOC/part reads must succeed using the user's existing session. Only bookmark state persists across app restarts; offline book storage, footnote navigation, cloud conflict behavior on a second physical device and e-ink timings remain future/device checks.

## Validation

- ARM EABI5 soft-float app compiled using the cached builder and existing firmware Qt libraries (`--pull=never`).
- `tools/tests/native-reader.cpp`: strict data parsing, parent/inline XPointer round trips, complete line coverage for nine width/font combinations, offline state, conflicting versions, edits during save and ambiguous network results. Optional private chunk argument validates real text anchors without committing book content.
- Isolated native Qt test opened an authorized real book using GET/OPTIONS only, restored the current remote pointer, and produced a native rendering. A request interceptor blocked all network writes. The same transport serialized/acknowledged a POST through a mocked `fetch`, with zero network POST requests.
- Tests do not demonstrate actual PB617 e-ink latency or a live cross-device write/read cycle. Those require reading on the device; the real cloud bookmark was not moved by test traffic.

A generic Linux Qt test build:

```
g++ -std=c++11 -fPIC tools/tests/native-reader.cpp -Isrc $(pkg-config --cflags --libs Qt5Widgets Qt5WebEngineWidgets) -o /tmp/native-reader-test
QT_QPA_PLATFORM=offscreen /tmp/native-reader-test
```

No cookies, private signing keys or book content belong in repository fixtures.
