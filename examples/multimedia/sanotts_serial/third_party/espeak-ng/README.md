# eSpeak-ng Translator Subset for TuyaOpen sanoTTS

This directory contains the minimal grapheme-to-phoneme (G2P) translator subset of eSpeak-ng (version 1.52.0), ported for TuyaOpen embedded targets (T5AI-Core) and host reference testing.

## Overview

- **Source**: upstream eSpeak-ng 1.52.0 (`mcu/ports/wasm/espeak` / `mcu/ports/esp32s3/firmware/components/espeak-ng` from sanoTTS commit `f73b8382bc98a1c8de3488ba30d727ab82587828`).
- **License**: GPL-3.0 (see `LICENSE`).
- **Scope**: Translator / G2P only (`espeak_TextToPhonemes`). Audio synthesizers (Klatt, MBROLA, Wavegen audio output, SpeechPlayer, async fifo) are disabled via `config.h` and omitted from the build.

## Included Files

- `config.h`: Minimal configuration (`USE_*=0`, `PACKAGE_VERSION "1.52.0"`).
- `include/`: eSpeak-ng public API headers (`espeak-ng/speak_lib.h`, `espeak-ng/espeak_ng.h`).
- `libespeak-ng/`: The 27 core translator `.c` and `.h` files:
  `common.c`, `mnemonics.c`, `error.c`, `ieee80.c`, `compiledata.c`, `compiledict.c`, `dictionary.c`, `encoding.c`, `intonation.c`, `langopts.c`, `numbers.c`, `phoneme.c`, `phonemelist.c`, `readclause.c`, `setlengths.c`, `soundicon.c`, `spect.c`, `ssml.c`, `synthdata.c`, `synthesize.c`, `tr_languages.c`, `translate.c`, `translateword.c`, `voices.c`, `wavegen.c`, `speech.c`, `espeak_api.c`.
- `ucd-tools/`: Unicode character database lookup subset:
  `case.c`, `categories.c`, `ctype.c`, `proplist.c`, `scripts.c`, `tostring.c`.

## Porting Patches Applied

1. **Flat File System Path Support (`voices.c`)**:
   Added direct lookup in `<data_path>/<voice_name>` so flat file systems (SPIFFS / Flash partitions) without subdirectories can locate `cmn` directly in the model directory.
2. **Data Directory Acceptance (`speech.c`)**:
   `check_data_path` accepts flat data directory when `<data_path>/phontab` is readable without requiring directory stat (`-EISDIR`).
3. **Optional Secondary English Fallback Silence (`dictionary.c`)**:
   Suppresses warning when optional secondary English fallback dictionary `en_dict` is not present in Chinese-only voice mode.
