# Porting plan: Python → C++ / Qt 6

The Python edition was about 15,700 lines. This rewrite is being done in stages. Each stage ships as a
working app.

## Stage 1: done (6.0.0)

| Python module | C++ | Notes |
|---|---|---|
| `core/extraction.py` | `core/extraction.*` | parity-tested against the Python output on real pages |
| `core/validation.py`, `core/source_scoring.py` | `core/validation.*` | parity-tested |
| `core/games.py` | `core/games.*` | parity-tested |
| `core/discovery.py` (web, RSS, Google News, Google Alerts, YouTube, robots, ETag, back-off, removals) | `core/discovery.*`, `core/html.*` | own HTML/feed parser; no BeautifulSoup |
| `core/intelligence.py` | `core/intelligence.*` | |
| `core/checker.py`, `core/rewards.py` | `core/checker.*` | |
| `core/discord_notify.py` | `core/discord.*` | per-game and update webhooks, system alerts |
| `protection/*` | `core/protection.*` | |
| `database/*` (schema v7) | `core/store.*` | reads and writes the Python edition's database |
| `core/config.py` (.env, config.json, merging new defaults) | `core/config.*` | |
| `core/scheduler.py`, `core/service.py`, `core/autostart.py` | `app/scanner.*`, `app/autostart.*`, tray icon | runs in the tray instead of a separate service |
| `redemption/browser.py` (Playwright) | `app/redeem.*` (Qt WebEngine) | fills the fields, reads the result; never submits |
| `dashboard/*` (Tkinter): main window, codes, sources, activity, Discord, settings, wizard | `app/*` | redesigned |
| `packaging/windows/*` (PyInstaller) | `build_windows.bat`, `packaging/installer.iss` | |

## Stage 2: done (6.1.0)

| Python | C++ |
|---|---|
| `core/renderer.py` (Playwright JS rendering) | `app/renderer.*`: Qt WebEngine, bridged to the scanner thread, capped per scan |
| `redemption/page_check.py` | Account page → **Test Global / Garena page** (counts the UID and code boxes and the CAPTCHA, types nothing) |
| `dashboard/account.py` | Account page: binding, today's redemptions, cooldown, pause, history |
| `core/checker.py` report + CSV | `reportRows()` / `toCsv()` (tested) → Codes → **Export CSV** |
| per-game tabs and counts, `games_page.py` routing | navigation badges (“BO7 · 3 ready”), older titles appear once they have codes, Discord → **Per game** channels and mute |

## Stage 3: done (6.2.0)

| Python | C++ |
|---|---|
| `core/ai.py` (13 presets, 3 wire formats, streaming, native web search, sources) | `core/ai.*`: request shapes and SSE parsing tested for all three formats |
| `core/helper.py`, `core/websearch.py` | `core/helper.*`: live events, free lookups (build sites, Google News, Wikipedia, pasted links), chat memory, saved answers (same files) |
| `core/research.py`, `core/sources_catalog.py`, `core/meta.py` | `core/research.*`, `core/content.*`: text extraction, crawling, cross-check and settings merge are **parity-tested** against the Python output |
| `core/screenshots.py` | `ScreenshotLibrary` (same folders and credit files) |
| `core/discord_studio.py` | `core/studio.*` (payloads, uploads as multipart, preview) |
| `dashboard/ai_page.py`, `meta_view.py`, `screenshot_gallery.py`, `discord_studio.py` | `app/content_pages.*`, all on a worker thread (`app/aiworker.*`) |

Weapon **stats** research (TTK tables) moves to stage 4 with the stats view.


## Stage 4: done (6.3.0)

| Python | C++ |
|---|---|
| `core/weapon_stats.py` | `core/stats.*`: stats from tables and (optionally) the AI, median across sites, change history, TTK and TTK curves; **parity-tested** |
| `core/patch_watch.py` | `core/patches.*`: patch notes from feeds/pages, buff/nerf per weapon, build freshness, auto re-research; **parity-tested** |
| `core/playstyle.py`, `core/performance.py` | `core/community.*` (playstyles, profile, recommend, match log, performance); **parity-tested** |
| `core/community.py` (GitHub issues, reactions, ETag cache, creators, follows) | `core/community.*`, tested against a fake GitHub |
| `core/push.py` (ntfy) | `pushSend` with per-event switches; new codes, patches and followed creators push automatically |
| `core/i18n.py` + `data/i18n/*.json` | `core/i18n.*` + `app/uitranslate.*` (translates widgets as they appear; Arabic right-to-left; AI fills the rest) |
| `dashboard/stats_view.py`, `for_you.py`, `community_page.py` | `app/stage4_pages.*` |
| `android/` (Kivy + python-for-android) | Qt for Android: `build_android.sh`, `android/AndroidManifest.xml`, data files inside the APK |

### Not done / known gaps

- The Qt app (desktop and Android) can't be compiled in the environment this was written in. Only the
  engine is compiled and tested there, so the first `build_windows.bat` / `build_android.sh` run is the
  first compile of the UI. If it reports an error, send the log.
- Each translation ships about 120 of the 480 UI texts. Settings → Language → **Translate the rest with AI**
  fills in the others.
- Android scans only while the app is open (no background service yet).

Both editions share the same data folder and database, so you can switch between them. Just don't run
both scanners at the same time.
