# Localization and voice architecture — Shipwright 9.1.1

## Text

The message system is driven by static XML/resource data and the message state machine:

```text
message XML / custom message table
  -> CustomMessage / CustomMessageManager
  -> CustomMessage_RetrieveIfExists
  -> Message_StartTextbox / Message_Update
  -> Message_DrawText / Message_DrawTextJPN
  -> font and message textures
```

Relevant resources include `soh/assets/xml/<version>/text/message_data_static.xml`, `elf_message_*`, `message_texture_static.xml`, `message_static.xml`, `nes_font_static.xml` and the related texture headers. The message parser handles control codes, page breaks, colors, sound effects and item icons.

The source tag 9.1.1 exposes `LANGUAGE_ENG`, `LANGUAGE_GER` and `LANGUAGE_FRA` in `z64.h`; the custom-message selection in `soh/soh/OTRGlobals.cpp` has cases for English, German and French. A Spanish value in an existing runtime configuration must not be treated as proof that Spanish is supported by this exact source checkout.

## Asset-only localization

Replacing or adding message XML/assets can cover text and visual localization where the existing language/resource model accepts the data. It does not automatically add a new language enum, menu label, font coverage, wrapping rules or custom-message branch. Spanish requires a source-and-asset acceptance pass, not only edited text files.

## Voice-over boundary

The current audio engine is the N64/Shipwright audio path (`audio_playback.c`, `audioMgr.c`, `audio_seqplayer.c`, `audio_load.c`, `audio_synthesis.c`, `audio_effects.c`) with LUS resource types for sequences, samples and soundfonts. `z_message_PAL.c` already emits message sound effects through `Audio_PlaySoundGeneral`, but that is not speech playback.

A pre-generated voice system needs a stable `MESSAGE_ID -> VOICE_FILE` mapping, a voice asset loader, lifecycle hooks for message open/page advance/close, cancellation on message skip, and language selection. The least invasive future seam is around `Message_StartTextbox` and message state transitions, with audio loading routed through the existing LUS audio resource layer. This is `SOURCE_CHANGE`, not a configuration-only feature.

Text-to-speech exists as an enhancement path, but it is not evidence of a shipped pre-generated voice library. Do not claim voice-over until the files, mapping, packaging and runtime playback are all verified.

