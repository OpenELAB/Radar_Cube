# TTS to WAV tool

This directory is used to generate voice prompts and beep sound resources for the reversing radar project. The final audio format used in the project is unified as:

- WAV
- 16 kHz
- 16-bit PCM
- Mono

## MiMo-V2.5-TTS

`mimo_tts_to_wav.py` is used to generate voice prompts through the official OpenAI-compatible TTS API of Xiaomi MiMo.

The script calls by default:

- Base URL: `https://api.xiaomimimo.com/v1`
- Endpoint: `/chat/completions`
- Default mode: `voice-design`
- Default model: `mimo-v2.5-tts-voicedesign`
- Default voice design prompt: a young female in-vehicle safety voice speaking standard Mandarin, clear and brisk, slightly fast, calm and restrained, without exaggerated delivery or drawn-out endings, suitable for short 1–2 second alerts; read only the input phrase without explanation or expansion.
- Default Audio Tag: None
- Default text optimization: off
- API Key environment variable: `MIMO_API_KEY`

MiMo API will return base64 encoded audio data in `choices[0].message.audio.data`. The script will first save this audio as a temporary WAV file, and then convert it to the standard format required by this project through `ffmpeg`.

According to the official V2.5 documentation, the text design voice mode uses `mimo-v2.5-tts-voicedesign`. The script places the sound design description in `user.content` and the broadcast text that needs to be synthesized in `assistant.content`. Text design voice mode does not pass the `voice` field.

To ensure that the in-vehicle short tone only reads the original text, the script does not enable `optimize_text_preview` by default, nor does it add audio tags by default. The official documentation states that `optimize_text_preview` will intelligently polish the text of the target broadcast, so it may cause "Radar Lost Contact" to be expanded into a longer sentence.

## Environment preparation

Install Python dependencies:

```powershell
pip install openai
```

If the machine does not have `ffmpeg` yet, install it first:

```powershell
winget install Gyan.FFmpeg
```

Set the MiMo API Key in the current PowerShell session:

```powershell
$env:MIMO_API_KEY="your_api_key"
```

Do not submit API Key or `.env` files to git.

## How to use

### Batch generate voices according to sound effect list

Batch generate the voice WAVs listed in `tts_to_wav/音频与音效清单.md`:

```powershell
$env:MIMO_API_KEY="your_api_key"
python .\tts_to_wav\gen_voice_from_sound_list.py
```

Default behavior:

- Read the Markdown table and generate entries with type `Voice` and nonempty spoken text.
- `Short Sound Effect` and `Distance Beep` entries are not generated.
- The current default list contains 19 voice resources.
- Output to `.\tts_to_wav\data`.
- An existing WAV with the same name will be overwritten.
- Use the MiMo built-in voice `茉莉`.

First check which files will be generated without calling the API:

```powershell
python .\tts_to_wav\gen_voice_from_sound_list.py --list
```

Skip existing files:

```powershell
python .\tts_to_wav\gen_voice_from_sound_list.py --skip-existing
```

Specify a source list, output directory, or voice:

```powershell
python .\tts_to_wav\gen_voice_from_sound_list.py --source .\tts_to_wav\音频与音效清单.md --out-dir .\tts_to_wav\data --voice 茉莉
```

### Interactive batch generation

Running without parameters will enter interactive mode:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py
```

It can also be specified explicitly:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py --interactive
```

After entering, first configure the mode, timbre, output directory, sampling rate and other parameters, and then keep inputting text to generate WAV:

```text
Text> Device not paired
Text> Controller battery low => power_low.wav
Text> :config
Text> q
```

Input rules:

- Enter text directly: automatically generate file names based on text.
- `text => filename.wav`: Specify the output file name for this text.
- `:config`: Re-enter the configuration process.
- `q` / `quit` / `exit`: Exit.

### Single command generation

Use the default text design sound to generate prompt sounds:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "Device not paired" -o .\tts_to_wav\data\mode_unpaired.wav
```

Custom text design tone:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "Controller battery low" -o .\tts_to_wav\data\power_low.wav --voice-design "Clear, concise English vehicle safety prompt with a calm female voice."
```

Enable text optimization preview:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "Device not paired" -o .\tts_to_wav\data\mode_unpaired.wav --optimize-text-preview
```

Note: `--optimize-text-preview` may polish or expand the broadcast text. It is generally not recommended to turn on radar beeps.

Use MiMo built-in tone compatibility mode:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "System fault" -o .\tts_to_wav\data\fault_system.wav --mode builtin --voice 茉莉
```

Specify the broadcast style of the built-in tone mode:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "Left sensor battery low" -o .\tts_to_wav\data\power_sensor_low_left.wav --mode builtin --voice 茉莉 --style "Clear, standard Mandarin, calm and restrained, suitable for a short, direct in-vehicle safety alert."
```

Specify the audio tag:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "Communication fault" -o .\tts_to_wav\data\fault_comm.wav --style-tags "calm clear concise"
```

Turn off the audio tag and just use normal text:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "Communication fault" -o .\tts_to_wav\data\fault_comm.wav --no-style-tags
```

Keep the original WAV returned by the MiMo API for easy inspection or comparison:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "Left sensor fault" -o .\tts_to_wav\data\fault_sensor_left.wav --keep-temp
```

When using `--keep-temp`, the original file is saved as `*.mimo_source.wav` next to the output file.

## Commonly used checking commands

View script parameters:

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py --help
```

Check script syntax:

```powershell
python -m py_compile .\tts_to_wav\mimo_tts_to_wav.py
```

## Existing script description

- `mimo_tts_to_wav.py`: Use Xiaomi MiMo official TTS API to generate speech.
- `gen_voice_from_sound_list.py`: Parse voice entries from `音频与音效清单.md` and generate WAV files in batches.
- `tts_to_wav.py`: The original Edge TTS speech generation script; running it without parameters starts configuration, followed by a loop for entering text to generate audio.
- `gen_beeps.py`: Use `ffmpeg` to generate or normalize beep resources.

## Notes

- The current script uses the `mimo-v2.5-tts-voicedesign` text to design the voice by default.
- `--voice` is only used under `--mode builtin`; the default text design tone mode does not pass `voice`.
- Voice cloning is not included in this tool to avoid introducing reference audio licensing risks.
- `optimize_text_preview` is turned off by default; turning it on may polish or expand the text.
- Audio tags are turned off by default; when `--style-tags` is specified, tags will go into `assistant.content`.
- Real audio generation requires network access and a valid MiMo API Key.
- If you are prompted that `ffmpeg` cannot be found, you need to restart the terminal and try again after installation.
