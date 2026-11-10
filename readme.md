# MMSFLC SoundMod — Expanded Music Options + External Custom Audio

![](preview.png)

适用于 Steam 版《流星洛克人 完美合集》(*Mega Man Star Force Legacy Collection*) 的音乐增强与外部自定义音频 Mod。
本项目基于 [Prof. 9 的 MMSFLC Expanded Music Options](https://github.com/Prof9/MMSFLC-Expanded-Music-Options) 深度扩展，在完整保留原版全部游戏内混搭、播放列表、偏好混音等功能的基础上，集成了原生外部音频与复古芯片音乐（Chiptune）引擎。

A comprehensive BGM customization and external custom music mod for *Mega Man Star Force Legacy Collection* (Steam).
Forked from and built on top of [Prof. 9's MMSFLC Expanded Music Options](https://github.com/Prof9/MMSFLC-Expanded-Music-Options), seamlessly adding external audio & retro chiptune playback directly into the native Audio Settings UI.

---

## 中文说明 (Chinese Guide)

### 核心特性
1. **原生菜单无缝集成（支持简中 / 繁中 / 日语 / 英语）**
   在游戏【声音设置 (Audio Settings)】中为全部场景类型新增两项模式：
   - **自定义音乐 (Custom Music)**：随机播放对应分类文件夹内的外部音乐（无连续重复）。**在该选项上按确认键可直接弹出 Windows 资源管理器打开对应音乐目录！**
   - **自定义 + 播放列表 (Custom + Playlist)**：将外部音乐文件夹与游戏内置自定义播放列表等概率混合随机播放。
   - **最爱 (Favorites) 增强**：若 `custom_music/favorites/` 目录中放入外部音乐，选择【最爱】时也会自动将其混入随机池。
2. **全格式音频与复古芯片音乐支持**
   - **常规音频**：`.ogg`、`.mp3`、`.wav`（支持 RIFF `smpl` 循环块）、`.flac`
   - **芯片音乐 (libgme)**：`.nsf`（含 MMC5 / VRC6 / VRC7 / N163 / FDS / Sunsoft 5B 扩展音源）、`.nsfe`、`.spc`、`.vgm`、`.vgz`（原生解压）、`.gbs`、`.hes`、`.ay`、`.kss`、`.sap`、`.gym`
3. **OGG / WAV 采样级无缝循环**
   - 自动识别 `.ogg` Vorbis Comments 循环标签（不区分大小写、下划线或连字符，支持嵌套在 `COMMENT=` 中）：
     - 循环起点：`LOOPSTART` / `LOOP_START` / `LOOPPOINT` / `LOOP`
     - 循环长度或终点：`LOOPLENGTH` / `LOOP_LENGTH` / `LOOPEND` / `LOOP_END`
     - 数值格式兼容采样帧数（`123456`）、秒（`12.5s`）、毫秒（`12500ms`）与时间戳（`01:23.456`）。
4. **零拷贝跨目录共享（`.lnk` 快捷方式 & `.m3u` 列表）与多曲目选曲**
   - **单文件 / 文件夹快捷方式**：按住 `Alt` 将任意音频文件或整个外部音乐目录拖入 `normal_battle`、`boss_battle` 等分类文件夹创建 `.lnk` 快捷方式（仅 ~1 KB），无需重复复制大文件。
   - **多曲目 `.nsf` / `.gbs` 选曲（十进制，从 `1` 开始）**：
     - 直接改文件名：`Rockman2#3.nsf`（播放第 3 首）
     - **快捷方式末尾加 `#曲目号`**：例如在 `normal_battle` 放 `Stage#3.lnk`，在 `boss_battle` 放 `Boss#18.lnk`，均指向同一个 `.nsf` 原文件即可分别播放不同曲目！
     - `.m3u` / `.m3u8` 播放列表：在分类目录新建文本列表，每行写相对或绝对路径（如 `../MegaMan2.nsf#18`）。
5. **原版音量对齐与生命周期同步**
   - 自动跟随游戏原生 **BGM 音量滑块**（采用与游戏 Wwise 总线一致的对数曲线与各芯片格式响度校准，不会盖过战斗音效），切场景淡出、暂停、切回标题画面完全同步。
   - 支持通过 `reframework/data/ExpandedMusicOptions/custom_music/overrides.json` 按十六进制 `TriggerID` 精确替换单首特定 BGM，或通过可选 `"volume_scale"` 微调全局增益（热重载）。

### 安装方法
1. 确保已安装最新 Nightly 版 **[REFramework](https://github.com/praydog/REFramework-nightly/releases)**（将压缩包中的 `dinput8.dll` 放入游戏根目录 `MegaMan StarForce LegacyCollection/`）。
2. 从 **[Releases](https://github.com/masuexe/mmsflc-soundmod/releases)** 下载最新发布包 `MMSFLC-SoundMod-v1.0.0.zip`：
   - **使用 Fluffy Mod Manager**：直接将 `.zip` 拖入 Fluffy Manager 5000 并启用 Mod；
   - **手动安装**：将压缩包解压至游戏根目录（确保 `ExpandedMusicOptions.dll` 位于 `reframework/plugins/ExpandedMusicOptions.dll`）。
3. 启动一次游戏后，插件会自动在 `reframework/data/ExpandedMusicOptions/custom_music/` 下创建全部 15 个分类文件夹、`README.txt` 与 `overrides.json` 模板。

---

## English Documentation

### What's New in MMSFLC SoundMod
- **External Custom Music Modes** integrated into all 4 UI languages (EN / JP / zh-TW / zh-CN):
  - **Custom Music**: Randomly plays external tracks from `reframework/data/ExpandedMusicOptions/custom_music/<category>/` without back-to-back repeats. **Press Confirm on this option to open the folder directly in Windows Explorer!**
  - **Custom + Playlist**: Mixes external custom files proportionally with your in-game custom playlist.
  - **Favorites Integration**: Any files placed in `custom_music/favorites/` are automatically mixed into the **Favorites** pool.
- **Wide Format & Chiptune Support**:
  - Standard audio: `.ogg`, `.mp3`, `.wav` (RIFF `smpl` loop chunk supported), `.flac`
  - Retro chiptunes (`libgme`): `.nsf` (including expansion audio chips), `.nsfe`, `.spc`, `.vgm`, `.vgz`, `.gbs`, `.hes`, `.ay`, `.kss`, `.sap`, `.gym`
- **Sample-Accurate Seamless Looping**:
  - Supports universal `.ogg` Vorbis loop tags (`LOOPSTART` / `LOOP_START` / `LOOPPOINT` + `LOOPLENGTH` / `LOOP_LENGTH` / `LOOPEND` / `LOOP_END` in samples, `s`, `ms`, or `mm:ss.xxx`).
- **Zero-Copy File Sharing (`.lnk` Shortcuts & `.m3u` Playlists)**:
  - Drop Windows `.lnk` shortcuts pointing to individual audio files, entire external music folders, or `.m3u` playlists into any category folder.
  - Select 1-based decimal sub-tracks in multi-track `.nsf`/`.gbs` archives via `Track#3.nsf`, shortcut suffixes (`Boss#18.lnk` -> plays track 18 of the target `.nsf`), or `.m3u` entries (`../Rockman2.nsf#3`).
- **Calibrated Native Volume & Exact Trigger Overrides**:
  - Automatically follows the in-game **BGM Volume** slider using a Wwise-matched loudness curve and per-format chiptune normalization so battle SFX stay clear.
  - Map specific hex `PlayTriggerID`s or tweak `"volume_scale"` in `custom_music/overrides.json` (hot-reloaded).

### Base Expanded Music Options Features (by Prof. 9)
All original features from **MMSFLC Expanded Music Options** remain fully intact:

> DLC BGM setting > Custom BGM type setting > 'BGM Selection' base option

For each BGM type (`Real World`, `Wave Road`, `Wave Space`, `Comp Space / Noise Wave`, `Normal Battle`, `Boss Battle`, `Final Battle`, `Victory`, `Defeat`, `Incident`, `Miscellaneous`, `Title Screen`, `Main Menu`), you can choose:
- **No Change** — Follows the base 'BGM Selection' option.
- **Always Original** / **Always Arranged** — Forces Original or Arranged BGM for this category.
- **Preferred Mix** — Press Confirm to pick Original, Arranged, or both per individual song in the Music Player.
- **Playlist** — Press Confirm to build a category-specific in-game BGM playlist in the Music Player.
- **Custom Music** — Plays external audio files from this category's `custom_music/<category>/` folder (press Confirm to open folder).
- **Custom + Playlist** — Combines external audio files with your in-game category playlist.
- **Favorites** — Plays songs from your shared Favorites list (plus `custom_music/favorites/`).
- **Music Off** — Mutes BGM for this category.

### Installing
1. Install **[REFramework](https://github.com/praydog/REFramework-nightly/releases)** (`dinput8.dll` in your `MegaMan StarForce LegacyCollection` game folder).
2. Download `MMSFLC-SoundMod-v1.0.0.zip` from **[Releases](https://github.com/masuexe/mmsflc-soundmod/releases)** and install via **Fluffy Mod Manager** or extract directly into the game directory.

### Save Data
Settings, playlists, and custom music folders are stored under `reframework/data/ExpandedMusicOptions/`.

### Building
1. Clone with submodules:
   ```powershell
   git clone --recurse-submodules https://github.com/masuexe/mmsflc-soundmod.git
   ```
2. Launch the game once with REFramework and dump SDK (`DeveloperTools -> ObjectExplorer -> Dump SDK`) so `Enums_Internal.hpp` exists in your game folder.
3. Configure and build with CMake & Visual Studio 2022 (C++23):
   ```powershell
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

### Credits & Licenses
- **[Prof. 9](https://github.com/Prof9/MMSFLC-Expanded-Music-Options)** — Original *MMSFLC Expanded Music Options* codebase and UI framework (MIT License, see [license.md](license.md)).
- **[praydog / REFramework](https://github.com/praydog/REFramework)** — RE Engine scripting & plugin framework (MIT License).
- **[mackron / miniaudio](https://github.com/mackron/miniaudio)** — Audio playback & decoding engine (Public Domain / MIT-0).
- **[libgme / Game_Music_Emu](https://github.com/libgme/game-music-emu)** — Video game music file emulators (LGPL-2.1-or-later; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
