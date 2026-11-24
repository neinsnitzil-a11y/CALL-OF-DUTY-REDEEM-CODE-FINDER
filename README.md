# COD Redemption Hunter — C++ / Qt 6 edition

A native rewrite of COD Redemption Hunter. It watches public sources around the clock for Call of Duty
redeem codes (CODM and every mainline title), checks how likely each one is to still work, posts them to
Discord, and opens the official redemption page with the code filled in. **You** solve the CAPTCHA and
press Redeem. It never submits for you.

> **Status: all four stages of the rewrite are in.** Everything the Python edition did now runs
> natively: scanner, checker, Discord, protection, AI helper, builds & settings research, screenshots,
> Discord studio, weapon stats & TTK, patch watch, playstyle picks, your match log, community builds on
> GitHub, phone push, 9 languages and an Android build. [PORTING.md](PORTING.md) has the details.

## The look (6.4)

A tactical, game-menu style built entirely from original art drawn in code. It has no Activision logos,
renders or screenshots, so the project can be shared and forked freely.

- **Six themes:** Warzone (gunmetal + orange), Black Ops (blackout + red), Ghost (night-vision green),
  Desert ops, Arctic (light) and Terminal. Each one has its own background: digital camo, woodland camo,
  a topographic map or a HUD grid.
- **Page banners:** each page gets a banner with the camo or map background, a weapon-class outline
  (AR, SMG, LMG, sniper, marksman, shotgun, pistol, launcher, knife), corner brackets and an
  UPPERCASE menu-style title.
- **Icons:** a line-icon set drawn in code, plus an icon side bar that collapses (Ctrl+B).
- **Command palette (Ctrl+K):** jump to any page, run an action, switch theme or search codes.
- **Polish:** animated stat cards, slide-in toasts and a tactical loading screen.
- **Make your own theme:** Settings → Theme → Customize. Pick every colour, the pattern and its strength,
  the corner rounding, text size, uppercase titles and the banner weapon. It previews live, saves as a
  small `.json` file, and you can export and import themes to share them.

## Customize everything

- **Sources:** edit any source (double-click it), filter the list, and export or import source lists to
  share them with friends.
- **Scoring & detection (Settings):** trust per kind of site, the score that counts as ready, the bonus
  when several sites list a code, code length limits, and a "never a code" word list. One click resets
  them to the defaults.
- **My builds (Builds & settings):** save your own loadouts. They show up on For you, scored against your
  playstyle, and you can export and import them.

## What's better than the Python edition

- **One native app, no Python.** No runtime, no `pip`, no virtual environment, nothing to repair.
- **Low memory and instant start-up.** The engine is plain C++20 with SQLite. Scanning runs on its own
  thread, so the window never freezes.
- **Runs from the tray around the clock.** A separate background service and scheduled task aren't
  needed. "Start with Windows" launches it minimised and scanning.
- **Modern interface (Qt 6):**
  - app bar with a live status pill
  - stat cards with animated counts that filter the list when clicked
  - navigation rail and a sortable codes table with a detail panel (reward, verdict, reasons, the sites
    that listed the code)
  - filter chips with counts and slide-in toasts
  - Dark, Hacker and Light themes, sharp on high-DPI screens
- **Reads JavaScript-built code pages.** The built-in browser draws pages that only show their codes
  after scripts run (a few per scan), so those codes aren't missed.
- **Account page.** Shows binding, today's redemptions, cooldown and any redemption pause, and can
  test the official pages without typing anything.
- **AI helper (any provider).** OpenAI, Claude, Gemini, OpenRouter, Groq, Mistral, DeepSeek, xAI,
  Together, Perplexity or a local server (Ollama, LM Studio). Answers type out live, with sources;
  providers that can search the web do it themselves, and the rest get free lookups (the game's build
  sites, Google News, Wikipedia, and any link you paste). Attach a screenshot and ask about it. The
  chat is remembered between sessions.
- **Builds & settings.** Hand-picked meta builds and settings per game, plus **Research**: reads the
  game's build guides and the 38-site catalog (robots.txt respected), pulls builds and settings from
  text and, with an AI, from page text and screenshots, then cross-checks them. A build listed by 2+
  sites is marked verified.
- **Screenshot library.** Build screenshots found by research (always credited to the site) and your
  own.
- **Discord studio.** Choose codes, builds, settings, screenshots or a message, preview exactly what
  Discord gets, pick the channel and send.
- **Weapon stats & time to kill.** Fire rate, damage by range, ADS, sprint-to-fire, velocity and mag from
  the stats sites (the median when sites disagree), time to kill for your health/armor, and a TTK-over-distance
  chart comparing two weapons. Changes the sites make are recorded.
- **Patch watch.** Patch notes are checked every few hours; buffs and nerfs are picked out per weapon, builds
  made before a patch that touched their weapon are marked **⚠ outdated**, and the stats are re-researched.
- **For you.** Pick controller / mouse & keyboard / touch, your playstyle and mode; every build we know
  (hand-picked, researched, community) is scored for you, with reasons. Log matches (kills, deaths, win) and
  your own results feed back into the picks.
- **Community builds on GitHub.** Players share builds as issues on a repo you choose: 👍 / 👎 votes ranked
  fairly (Wilson score), creator pages, follow a creator, submit from the app (with a token) or the browser.
  No server to run.
- **Phone push (ntfy).** Free, no account: new codes, patch buffs/nerfs and builds from creators you follow.
- **9 languages.** English, Español, Português, Bahasa Indonesia, हिन्दी, Français, Deutsch, العربية
  (right-to-left), Türkçe. Untranslated text can be filled in with your AI provider in one click.
- **Android.** The same app built with Qt for Android (`build_android.sh`, or `build_android.bat` via WSL).
- **CSV export** of every code with its verdict and reasons, for Excel or Sheets.
- **Redemption inside the app (Qt WebEngine).** The official page opens in a window with your UID and
  the code filled in. The app reads the result text back and asks you to confirm it.
- **Same data as before.** It uses the same `config.json`, `.env` and `hunter.sqlite3` (schema v7) in
  `%LOCALAPPDATA%\COD Redemption Hunter`. Install it over the Python edition and your codes, settings
  and history carry over.

Scanning speed itself is about the same as before. A scan is mostly spent waiting on websites, and the
app also waits a polite delay between sites on purpose.

## Build it

### Windows: one click

Double-click **`build_windows.bat`**. It:

1. finds the Visual Studio 2022 C++ Build Tools, or offers to install them with `winget` (free, about 3 GB)
2. downloads Qt 6.8 with WebEngine into `.qt\` using `aqtinstall` (needs Python once; about 1.5 GB, one time)
3. builds with CMake and runs the core tests
4. collects the app into `dist\CODRedemptionHunter\` (windeployqt plus the Visual C++ runtime DLLs)
5. compiles **`dist\installer\COD-Redemption-Hunter-Setup-6.4.3.exe`** with Inno Setup 6, installing
   Inno Setup with `winget` if it's missing

If you already have Qt, run `set HUNTER_QT_DIR=C:\Qt\6.8.3\msvc2022_64` first.

**Or let GitHub build it.** Push the project and open **Actions → Build**. The Windows job uploads the
installer as an artifact.

### Linux / macOS

```bash
# Debian / Ubuntu / Kali
sudo apt install build-essential cmake ninja-build libsqlite3-dev qt6-base-dev qt6-webengine-dev
./build.sh
```

Without Qt you can still build and test the engine: `cmake -S . -B build -DHUNTER_BUILD_APP=OFF`.

### Android APK

```bash
./build_android.sh            # Linux / WSL; Windows: double-click build_android.bat (uses WSL)
```

It downloads everything it needs into `~/.cod-hunter-android` (no sudo needed) (JDK 17, Android SDK + NDK 26.1, Qt 6.8.3 for
Android) and writes `dist/COD_Redemption_Hunter-6.4.3-arm64.apk`. `--release` signs with your keystore
(see the top of the script). GitHub Actions builds the APK too. The Android app has no built-in browser:
redemption pages open in your phone's browser with the code copied. Scanning runs while the app is open;
for 24/7 alerts on your phone keep the PC app running and turn on phone push.

## Use it

- First start: a short setup wizard asks for your CODM UID (optional), a Discord webhook (optional) and
  whether to start with Windows.
- **F5** scans now. **Ctrl+F** searches codes. **Ctrl+I** opens the AI helper. **Ctrl+K** opens the command palette. **Ctrl+B** collapses the side bar. **Ctrl+T** switches theme. **Ctrl+Q** quits. Double-click a code to
  redeem it.
- Settings → **Language** switches the app language; **Phone push** makes your ntfy topic.
- Community builds: create a public GitHub repo for your group, then Community → ⚙ Set up. A token is
  optional (fine-grained, Issues read & write on that repo only) and stays in your `.env`.
- Closing the window keeps it scanning in the tray. To quit, right-click the tray icon and choose Quit.
- `CODRedemptionHunter --scan-once` runs one scan from the command line and prints new codes, with no
  window. Use it for your own schedulers.

## Safety (unchanged from the Python edition)

- It never solves or bypasses a CAPTCHA and never presses Redeem for you.
- It reads only public pages over HTTPS, follows `robots.txt`, sends `If-None-Match` /
  `If-Modified-Since`, and backs off for hours when a site answers 403 or 429.
- It binds to one UID. Daily cap, cooldown and circuit breaker are all stored in SQLite, so restarting
  never resets them. **Emergency stop** blocks redemption until you clear it.
- Your UID, webhooks and keys live only in your `.env`. None of them are in the installer.

## Project layout

```
core/        scanner engine, plain C++20 + SQLite (no Qt), unit-tested
  json · util (time, URLs, SHA-256, entities) · games · extraction · validation (+ source scoring)
  html (HTML → blocks, RSS/Atom, robots.txt) · store (SQLite, schema v7) · intelligence
  discovery (web · rss · google_news · google_alerts · youtube) · checker (+ rewards)
  protection (UID binding, rate limits, circuit breaker) · discord · config · engine
  ai (any AI API, streaming) · helper (chat + free web lookups) · research (builds & settings)
  content (builds file, site catalog, screenshot library) · studio (Discord studio)
  stats (weapon stats, TTK) · patches (patch watch, build freshness) · community (GitHub builds,
  ntfy push, playstyle picks, match log) · i18n (translations)
app/         Qt 6 desktop app
  main · mainwindow · pages (codes, sources, activity, discord, account, settings) · content_pages (AI
  helper, builds & settings, screenshots, Discord studio) · aiworker · codesmodel · widgets
  theme · scanner (worker thread) · qthttp (Qt Network) · redeem (WebEngine window) · renderer
  (JavaScript pages, official-page test) · wizard · autostart · stage4_pages (stats & patches, for
  you, community) · art (icons, weapon outlines, camo, emblem) · themeeditor · commandpalette · uitranslate (translates every window)
tests/       core_tests.cpp + golden.json and real code pages (parity with the Python edition)
data/        shipped config.json, .env.example, meta.json (builds & settings), research_sources.json,
             i18n/ (translations + strings.json, the list of English texts)
android/     AndroidManifest.xml
packaging/   installer.iss (Inno Setup 6)
build_windows.bat · build.sh · build_android.sh/.bat · CMakeLists.txt · .github/workflows/build.yml
```

## Tests

`ctest --test-dir build` runs 32 test groups (378 checks) offline. They include **parity tests**:
`tests/golden.json`, `golden_research.json` and `golden_stage4.json` hold the Python edition's output on four real code pages and a set of tricky
texts, and the C++ engine has to match it exactly. That covers every code, region, report
(active/expired/used up), expiry date, game, platform and kind. Discovery and Discord are tested
against a fake network: baseline reads, ETags, 429 back-off, codes taken down, Google Alerts, the
connect list, new-code posts and dead-code updates.
