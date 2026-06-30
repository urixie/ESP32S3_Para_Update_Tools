---
name: esp32s3-param-bin-tool
description: Project-specific workflow for the ESP32-S3 UniEdge encrypted parameter bin desktop tool in this workspace. Use when editing the Tauri/Rust backend, React parameter builder/parser UI, project JSON handling, AES-256-GCM bin export/parse logic, board-name or parameter validation, protocol documentation, tests, or release packaging in this tools project.
---

# ESP32-S3 Param Bin Desktop Tool

## Scope

Use this skill for the project at:

`E:\work\ESP32S3_Para_Update_Tools\tools`

This is a Tauri 2 + Rust + React + TypeScript desktop tool for building and parsing encrypted UniEdge ESP32-S3 parameter `.bin` files.

The tool is paired with matching ESP32 firmware. Keep the PC-side bin protocol, Rust backend, frontend types, and firmware-facing documentation aligned whenever changing file format, validation, encryption, or parameter semantics.

## Source Encoding And Tools

For source reading and editing on Windows, prefer `rg` for search.

Avoid PowerShell text rewriting for files that contain Chinese UI strings or protocol text because the console code page can display mojibake. Use `apply_patch` for manual edits. If scripting is unavoidable, read and write UTF-8 explicitly.

If terminal output shows mojibake, verify against UTF-8 source bytes, Rust tests, or application behavior instead of trusting console rendering.

## Project Layout

- React app entry: `src/App.tsx`
- Parameter builder page: `src/pages/BuilderPage.tsx`
- Bin parser page: `src/pages/ParserPage.tsx`
- Shared parameter table UI: `src/components/ParamTable.tsx`
- Frontend type/constants mirror: `src/types/parameter.ts`
- Main styles: `src/styles/app.css`, `src/styles/theme.css`
- Tauri commands: `src-tauri/src/commands.rs`
- Rust data model and constants: `src-tauri/src/model.rs`
- Project JSON save/load: `src-tauri/src/project_file.rs`
- Parameter validation: `src-tauri/src/validator.rs`
- Bin container format: `src-tauri/src/bin_format.rs`
- Payload binary codec: `src-tauri/src/payload_codec.rs`
- AES-GCM helpers and product key: `src-tauri/src/crypto.rs`
- Rust regression tests: `src-tauri/src/tests.rs`
- Firmware-facing protocol document: `docs/bin_protocol.md`
- Packaging helpers: `release/`

Generated outputs live in `dist/`, `src-tauri/target/`, and `release/`. Do not treat them as source unless the user is explicitly packaging or inspecting release artifacts.

## Runtime Contract

The UI has three top-level views:

1. `BuilderPage` creates, edits, saves, loads, validates, and exports parameter projects.
2. `ParserPage` opens an encrypted `.bin`, decrypts it, displays board name and parameters, and can reuse parsed data in the builder.
3. `AboutPage` contains product/version context.

Frontend state uses camelCase TypeScript fields. Rust structs use serde camelCase so `ProjectFile`, `Parameter`, `ParsedBinInfo`, and `ValidationError` round-trip without ad-hoc conversion.

Keep visible UI copy Chinese-first. Keep English for technical labels such as `bin`, `Payload`, `Header`, AES-GCM, and file extensions.

## Parameter And Project Rules

Current business constants:

- `PARAM_COUNT = 72`
- addresses are fixed and complete: `0..71`
- `defaultValue` is `u16`: `0..65535`
- parameter name limit is 30 Unicode characters and 96 UTF-8 bytes
- board name limit is 32 Unicode characters and 96 UTF-8 bytes
- `paramType`: `control` -> `0`, `protection` -> `1`
- `permission`: `hidden` -> `0`, `visible` -> `1`

Project files are plaintext engineer-side JSON (`.ueproj.json`). They are not the ESP32 runtime file format. It is acceptable for project JSON to contain board names and parameter names in plaintext.

Do not store `PRODUCT_KEY` in React or TypeScript. Keep key material in Rust backend and matching ESP32 firmware only.

## Current Bin Protocol

Treat the Rust implementation and tests as the source of truth before changing docs.

Current container format in `src-tauri/src/bin_format.rs`:

- file layout: `[17-byte Header][AES-GCM ciphertext][16-byte GCM tag]`
- magic: `UEPB`
- `format_version`: `1`
- nonce: 12 random bytes at Header offset 5
- AAD: the complete 17-byte Header
- tag length: 16 bytes

Current encrypted payload schema in `src-tauri/src/payload_codec.rs`:

- `SCHEMA_VERSION = 2`
- `PAYLOAD_HEADER_LEN = 20`
- payload magic: `UPLD`
- record size: 12 bytes
- payload contains encrypted board name, then encrypted parameter name table

`docs/bin_protocol.md` may describe an older 48-byte Header and schema v1. If protocol work touches docs or ESP32 parser guidance, first reconcile it with `bin_format.rs`, `payload_codec.rs`, and `tests.rs`; do not silently resurrect the old 48-byte layout unless the user explicitly asks for a protocol rollback.

## Validation And Security

Keep validation centralized in Rust:

- frontend may clamp input for usability
- export must still call Rust validation before writing a bin
- parser must reject wrong magic, unsupported format/schema versions, malformed payloads, invalid UTF-8, duplicate/missing/out-of-range addresses, and AES-GCM tag failures

Keep tests for these invariants:

- payload round-trip preserves board name and all 72 parameters
- full export/parse round-trip succeeds
- compact Header remains 17 bytes
- Chinese board/parameter names do not appear in bin bytes
- tampering with Header, ciphertext, or tag fails parse
- duplicate, missing, empty-name, and overlength-name cases fail validation
- default project validates

## Frontend Rules

Keep the app as an operational desktop tool, not a landing page.

Preserve the dense two-column 72-parameter workflow unless the user asks for a broader UI redesign. Avoid adding decorative marketing sections or explanatory in-app text that duplicates README content.

When adding fields that affect export or parse, update all relevant layers together:

- `src/types/parameter.ts`
- React page/component state and inputs
- `src-tauri/src/model.rs`
- validation or codec modules if the field is serialized
- tests and docs

Use Tauri dialog APIs from `@tauri-apps/plugin-dialog` for local file picking. Backend file access should stay behind Tauri commands.

## Validation Commands

From the project root:

```powershell
npm run build
```

For Rust backend tests:

```powershell
cd src-tauri
cargo test
```

For desktop runtime checks:

```powershell
npm run tauri:dev
```

For release packaging:

```powershell
npm run tauri:build
```

If only Rust protocol or validation changed, run `cargo test` at minimum. If TypeScript types, React pages, or build config changed, also run `npm run build`.
