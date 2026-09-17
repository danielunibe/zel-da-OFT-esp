# Control Codes Inventory

## OoT Native Control Codes

| Code | Hex | Name | Description |
|------|-----|------|-------------|
| `01` | 0x01 | NEWLINE | See message_data_fmt.h |
| `02` | 0x02 | END | See message_data_fmt.h |
| `04` | 0x04 | BOX_BREAK | See message_data_fmt.h |
| `05` | 0x05 | COLOR | See message_data_fmt.h |
| `06` | 0x06 | SHIFT | See message_data_fmt.h |
| `07` | 0x07 | TEXTID | See message_data_fmt.h |
| `08` | 0x08 | QUICKTEXT_ENABLE | See message_data_fmt.h |
| `09` | 0x09 | QUICKTEXT_DISABLE | See message_data_fmt.h |
| `0A` | 0x0A | PERSISTENT | See message_data_fmt.h |
| `0B` | 0x0B | EVENT | See message_data_fmt.h |
| `0C` | 0x0C | BOX_BREAK_DELAYED | See message_data_fmt.h |
| `0D` | 0x0D | AWAIT_BUTTON_PRESS | See message_data_fmt.h |
| `0E` | 0x0E | FADE | See message_data_fmt.h |
| `0F` | 0x0F | NAME | See message_data_fmt.h |
| `10` | 0x10 | OCARINA | See message_data_fmt.h |
| `11` | 0x11 | FADE2 | See message_data_fmt.h |
| `12` | 0x12 | SFX | See message_data_fmt.h |
| `13` | 0x13 | ITEM_ICON | See message_data_fmt.h |
| `14` | 0x14 | TEXT_SPEED | See message_data_fmt.h |
| `15` | 0x15 | BACKGROUND | See message_data_fmt.h |
| `16` | 0x16 | MARATHON_TIME | See message_data_fmt.h |
| `17` | 0x17 | RACE_TIME | See message_data_fmt.h |
| `18` | 0x18 | POINTS | See message_data_fmt.h |
| `19` | 0x19 | TOKENS | See message_data_fmt.h |
| `1A` | 0x1A | UNSKIPPABLE | See message_data_fmt.h |
| `1B` | 0x1B | TWO_CHOICE | See message_data_fmt.h |
| `1C` | 0x1C | THREE_CHOICE | See message_data_fmt.h |
| `1D` | 0x1D | FISH_INFO | See message_data_fmt.h |
| `1E` | 0x1E | HIGHSCORE | See message_data_fmt.h |
| `1F` | 0x1F | TIME | See message_data_fmt.h |

## Custom Message Format Codes

| Code | Name | Description |
|------|------|-------------|
| `&` | NEWLINE | Line break within page |
| `^` | PAGE_BREAK | Wait for input, new page |
| `%w` | WHITE | White text |
| `%r` | RED | Red text |
| `%g` | GREEN | Green text (adjustable) |
| `%b` | BLUE | Blue text |
| `%c` | LIGHTBLUE | Light blue text |
| `%p` | PINK | Pink text |
| `%y` | YELLOW | Yellow text |
| `%B` | BLACK | Black text |
| `##` | COLOR_PLACEHOLDER | Replaced by stored color |
| `[[var]]` | VARIABLE | Dynamic variable insertion |
| `$icon` | ALTAR_ICON | Item icon display |

## Codes Found in Corpus

| Code | Occurrences |
|------|-------------|
| `END` | 1989 |
| `NEWLINE` | 1804 |
| `COLOR` | 1056 |
| `COLOR(UNKNOWN_64)` | 1054 |
| `QUICKTEXT_DISABLE` | 583 |
| `UNSKIPPABLE` | 573 |
| `QUICKTEXT_ENABLE` | 571 |
| `BOX_BREAK` | 566 |
| `COLOR(UNKNOWN_65)` | 549 |
| `COLOR(UNKNOWN_68)` | 324 |
| `SHIFT` | 266 |
| `EVENT` | 218 |
| `COLOR(UNKNOWN_66)` | 213 |
| `NAME` | 142 |
| `TWO_CHOICE` | 139 |
| `TEXTID` | 128 |
| `COLOR(UNKNOWN_70)` | 113 |
| `ITEM_ICON` | 109 |
| `COLOR(UNKNOWN_67)` | 101 |
| `FADE` | 88 |
| `SFX` | 56 |
| `PERSISTENT` | 55 |
| `TEXT_SPEED` | 35 |
| `COLOR(UNKNOWN_69)` | 28 |
| `BOX_BREAK_DELAYED` | 28 |
| `OCARINA` | 18 |
| `FISH_INFO` | 12 |
| `TIME` | 10 |
| `HIGHSCORE` | 9 |
| `THREE_CHOICE` | 6 |
| `TOKENS` | 5 |
| `RACE_TIME` | 5 |
| `BACKGROUND` | 3 |
| `POINTS` | 3 |
| `MARATHON_TIME` | 2 |
| `FADE2` | 1 |
| `COLOR(UNKNOWN_8)` | 1 |
| `AWAIT_BUTTON_PRESS` | 1 |
