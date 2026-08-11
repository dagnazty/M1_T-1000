# Custom IR Remotes

The M1 can build, learn, replay, and edit user-defined IR remotes on the device.
Custom remotes are stored as standard Flipper Zero `.ir` files at `0:/IR/*.ir`
(the SD-card root of the IR folder, alongside the shipped category directories),
so they interoperate with the Flipper IRDB format.

Everything below is reachable from **Infrared → Custom Remotes**. The original
three Infrared entries (Universal Remotes, Learn, Replay) are unchanged.

## Create a remote

1. **Infrared → Custom Remotes** opens the *My Remotes* manager: `[+ New Remote]`
   followed by every `0:/IR/*.ir` file.
2. Select `[+ New Remote]`, type a name on the on-screen keyboard, confirm.
   - The name is sanitized to a FAT-legal filename (illegal characters → `_`,
     trailing spaces/dots trimmed, empty → `Remote`).
   - Collisions are de-duplicated as `Name_1 … Name_99`.
   - A valid, empty (header-only) `.ir` file is written. Escaping the keyboard
     writes nothing.

## Open a remote

Selecting a remote opens its action menu:

- **Play Buttons** — the shared scrolling button-list replay screen (the same
  engine the Universal Remotes browser uses). UP/DOWN navigate, OK transmits the
  selected button. Parsed and raw buttons both transmit.
- **Learn Button** — capture a new button (below).
- **Edit Buttons** — rename/delete buttons (below).

## Learn a button

From **Learn Button**, point a remote at the M1 and press a key.

- **Parsed capture (preferred):** if IRMP decodes the signal, the protocol,
  address, and command are shown. OK prompts for a name (auto-suggested
  `<Protocol>_<Command>`, editable) and appends the parsed button to the file.
- **Raw fallback (on UNKNOWN):** if IRMP cannot decode the frame, the raw
  mark/space edge stream is captured instead and shown as a sample count. OK
  prompts for a name and appends it as a `raw` signal (38 kHz / 0.33 duty by
  default). Raw buttons replay through the raw transmitter.

  A raw capture is only finalized on the inter-frame gap and only if it has at
  least a handful of edges — shorter bursts are treated as noise and ignored, so
  stray IR does not create junk buttons.

BACK/LEFT at any point leaves the file untouched.

## Edit buttons

**Edit Buttons** lists the remote's buttons. Select one to open its action menu:

- **Rename** — the current name is pre-filled on the keyboard; confirm to rewrite
  the file with the new name. ESC cancels with no file change.
- **Delete** — asks for confirmation first; confirming rewrites the file without
  that button. Cancel/BACK leaves the file untouched. Deleting the last button
  leaves a valid, empty (header-only) remote you can keep learning into.

All edits use an atomic read-all → temp-file → rename rewrite, so an interrupted
edit never corrupts the original file. Every other button's name, type, data, and
order are preserved exactly.

## Limits

- Up to 64 buttons are listed per remote in the editor.
- Raw signals hold up to 512 mark/space samples.
- Names are truncated to 31 characters.
- Custom remotes live at the `0:/IR/` root; the shipped category folders
  (`TV/`, `Audio/`, `Bluray/`, …) are browsed via **Universal Remotes**.

## Host validation

The `.ir` file layer is covered by host round-trip tests and a validator:

```
sh tools/host_test/run_tests.sh   # append / rewrite / rename / delete / raw
sh tools/host_test/validate.sh    # unit suite + validate every shipped .ir
```

`validate.sh` also parses every file in `ir_database/`, the same check applied to
M1-authored output (valid header + at least one parseable signal).
