# TTS 转 WAV 工具

这个目录用于生成倒车雷达项目的语音提示和蜂鸣音资源。项目最终使用的音频格式统一为：

- WAV
- 16 kHz
- 16-bit PCM
- 单声道

## MiMo-V2.5-TTS

`mimo_tts_to_wav.py` 用于通过小米 MiMo 官方 OpenAI-compatible TTS API 生成语音提示音。

脚本默认调用：

- Base URL: `https://api.xiaomimimo.com/v1`
- Endpoint: `/chat/completions`
- 默认模式: `voice-design`
- 默认模型: `mimo-v2.5-tts-voicedesign`
- 默认文本设计音色: `年轻女性车载安全提示音，标准普通话，声音清亮干练，吐字清晰，语速略快，语气冷静克制，不夸张，不拖尾，适合 1-2 秒短提示；严格只朗读输入短语，不添加解释或扩写。`
- 默认音频标签: 无
- 默认文本优化: 关闭
- API Key 环境变量: `MIMO_API_KEY`

MiMo API 会在 `choices[0].message.audio.data` 中返回 base64 编码的音频数据。脚本会先把这段音频保存为临时 WAV 文件，再通过 `ffmpeg` 转换为本项目要求的标准格式。

根据官方 V2.5 文档，文本设计音色模式使用 `mimo-v2.5-tts-voicedesign`。脚本把音色设计描述放在 `user.content`，把需要合成的播报文本放在 `assistant.content`。文本设计音色模式不传 `voice` 字段。

为保证车载短提示音严格只读原文，脚本默认不启用 `optimize_text_preview`，也不默认添加音频标签。官方文档说明 `optimize_text_preview` 会智能润色目标播报文本，因此它可能导致“雷达失联”被扩写成更长句子。

## 环境准备

安装 Python 依赖：

```powershell
pip install openai
```

如果本机还没有 `ffmpeg`，先安装：

```powershell
winget install Gyan.FFmpeg
```

在当前 PowerShell 会话中设置 MiMo API Key：

```powershell
$env:MIMO_API_KEY="你的_api_key"
```

不要把 API Key 或 `.env` 文件提交到 git。

## 使用方法

### 按音效清单批量生成语音

根据 `tts_to_wav/音频与音效清单.md` 批量生成所有语音类 WAV：

```powershell
$env:MIMO_API_KEY="你的_api_key"
python .\tts_to_wav\gen_voice_from_sound_list.py
```

默认行为：

- 根据 Markdown 表头读取资源，只生成 `类型` 为 `语音` 且“播报内容”非空的条目。
- 不生成 `短音效` 和 `距离蜂鸣` 条目。
- 当前默认清单包含 22 条语音资源。
- 输出到 `.\tts_to_wav\data`。
- 已有同名 WAV 会被覆盖。
- 使用 MiMo 内置音色 `茉莉`。

先查看将生成哪些文件，不调用 API：

```powershell
python .\tts_to_wav\gen_voice_from_sound_list.py --list
```

跳过已有文件：

```powershell
python .\tts_to_wav\gen_voice_from_sound_list.py --skip-existing
```

指定清单、输出目录或音色：

```powershell
python .\tts_to_wav\gen_voice_from_sound_list.py --source .\tts_to_wav\音频与音效清单.md --out-dir .\tts_to_wav\data --voice 茉莉
```

### 交互式批量生成

无参数运行会进入交互模式：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py
```

也可以显式指定：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py --interactive
```

进入后先配置一次模式、音色、输出目录、采样率等参数，然后一直输入文本生成 WAV：

```text
文本> 雷达失联
文本> 左侧雷达配对成功 => pair_ok_left.wav
文本> :config
文本> q
```

输入规则：

- 直接输入文本：自动按文本生成文件名。
- `文本 => 文件名.wav`：为这条文本指定输出文件名。
- `:config`：重新进入配置流程。
- `q` / `quit` / `exit`：退出。

### 单条命令生成

使用默认文本设计音色生成提示音：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "雷达失联" -o .\tts_to_wav\data\link_lost_both.wav
```

自定义文本设计音色：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "电量低" -o .\tts_to_wav\data\low_battery_left.wav --voice-design "年轻女性车载安全提示音，普通话标准，声音清晰明亮，语气冷静，短促直接。"
```

启用文本优化预览：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "雷达失联" -o .\tts_to_wav\data\link_lost_both.wav --optimize-text-preview
```

注意：`--optimize-text-preview` 可能会润色或扩写播报文本。雷达提示音通常不建议开启。

使用 MiMo 内置音色兼容模式：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "雷达故障" -o .\tts_to_wav\data\fault_left.wav --mode builtin --voice 茉莉
```

指定内置音色模式的播报风格：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "左侧雷达配对成功" -o .\tts_to_wav\data\pair_ok_left.wav --mode builtin --voice 茉莉 --style "清晰、标准普通话、冷静克制，适合车载安全提示音，短促直接。"
```

指定音频标签：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "雷达失联" -o .\tts_to_wav\data\link_lost_both.wav --style-tags "平静 干练 清亮"
```

关闭音频标签，只使用普通文本：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "雷达失联" -o .\tts_to_wav\data\link_lost_both.wav --no-style-tags
```

保留 MiMo API 返回的原始 WAV，方便检查或对比：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py "雷达故障" -o .\tts_to_wav\data\fault_left.wav --keep-temp
```

使用 `--keep-temp` 时，原始文件会保存为输出文件旁边的 `*.mimo_source.wav`。

## 常用检查命令

查看脚本参数：

```powershell
python .\tts_to_wav\mimo_tts_to_wav.py --help
```

检查脚本语法：

```powershell
python -m py_compile .\tts_to_wav\mimo_tts_to_wav.py
```

## 现有脚本说明

- `mimo_tts_to_wav.py`: 使用小米 MiMo 官方 TTS API 生成语音。
- `gen_voice_from_sound_list.py`: 从 `音频与音效清单.md` 按资源类型解析语音条目并批量生成 WAV。
- `tts_to_wav.py`: 原有 Edge TTS 语音生成脚本；无参数运行时也会先配置一次，然后循环输入文本生成。
- `gen_beeps.py`: 使用 `ffmpeg` 生成或归一化蜂鸣音资源。

## 注意事项

- 当前脚本默认使用 `mimo-v2.5-tts-voicedesign` 文本设计音色。
- `--voice` 只在 `--mode builtin` 下使用；默认文本设计音色模式不会传 `voice`。
- 语音克隆没有放进这个工具，避免引入参考音频授权风险。
- `optimize_text_preview` 默认关闭；开启后可能润色或扩写文本。
- 音频标签默认关闭；指定 `--style-tags` 后，标签会进入 `assistant.content`。
- 真实生成音频需要网络访问和有效的 MiMo API Key。
- 如果提示找不到 `ffmpeg`，安装后需要重启终端再试。
