# G02B — Pilot Message ID Reconciliation

PILOT_ID_0x1036: WRONG_PILOT

## Evidence

| Field | Finding |
|---|---|
| ID | `0x1036` |
| Namespace/table | English NES/NTSC message table, the `10xx` Forest/Kokiri dialogue range; Ship loads it through `text/nes_message_data_static/nes_message_data_static` |
| Source actor | `soh/src/overlays/actors/ovl_En_Ko/z_en_ko.c`, `ENKO_TYPE_CHILD_7` returns `0x1035`; the choice continuation selects `0x1036` |
| Call site | `EnKo` choice flow and `z_actor.c` choice continuation `0x1035 -> 0x1036` |
| English text | “The three yellow icons in the upper right are called [C] icons. They display the things you can use with the [C-Left], [C-Down] and [C-Right] buttons...” |
| Control codes | `[C]`, `[C-Left]`, `[C-Down]`, `[C-Right]`; no `[Z]` control occurrence |
| Scene/context | Kokiri Forest child tutorial/dialogue actor; the message explains C-button icons |
| Z glyph occurrence | None in the message text |

The earlier G02 description “Existing tutorial Z prompt” was incorrect for `0x1036`. The number is not a separate localization namespace collision in the evidence available here; it is the same `10xx` Forest/Kokiri text ID. The other “play Ocarina” reference was a cross-report identification error, not a reason to alter localization.

## Correct observation pilot

`0x100D` — existing Kokiri tutorial message:

> When a fairy flies near a person or thing, press [Z] to look in that direction. If you use [Z] Targeting, you can talk to people from a distance...

Source actor/call site: `soh/src/overlays/actors/ovl_En_Ko/z_en_ko.c`; `ENKO_TYPE_CHILD_5` returns `0x100D` after the relevant information flag is set. It is used only to observe the central semantic Z glyph path. No message data or localization file was changed.

The English text and ID cross-check was performed against the CloudModding OoT text-ID dump, which notes that its dump is NTSC 1.0 and that dialog can vary by version: https://wiki.cloudmodding.com/oot/Text_Ids_(1000-4000)
