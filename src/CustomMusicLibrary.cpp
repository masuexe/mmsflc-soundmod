#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlguid.h>
#include <shobjidl.h>

#include <nlohmann/json.hpp>

#include "Common.hpp"
#include "CustomAudioPlayer.hpp"
#include "CustomMusicLibrary.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <string_view>
#include <unordered_set>

namespace
{
	constexpr std::array<std::string_view, 15> ALL_CATEGORIES = {
		"real_world",
		"wave_road",
		"wave_space",
		"dungeon",
		"normal_battle",
		"boss_battle",
		"last_battle",
		"winner",
		"loser",
		"incident_real_world",
		"incident_wave_world",
		"miscellaneous",
		"title_screen",
		"main_menu",
		"favorites",
	};

	std::string toLowerStr(std::string_view sv)
	{
		std::string s(sv);
		std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	std::string_view trimAscii(std::string_view sv)
	{
		while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
		{
			sv.remove_prefix(1);
		}
		while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
		{
			sv.remove_suffix(1);
		}
		return sv;
	}

	std::filesystem::path utf8ToPath(std::string_view utf8Str)
	{
		return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(utf8Str.data()), utf8Str.size()));
	}

	std::string extLowerAscii(const std::filesystem::path &p)
	{
		std::u8string u8ext = p.extension().u8string();
		std::string s(reinterpret_cast<const char *>(u8ext.data()), u8ext.size());
		return toLowerStr(s);
	}

	std::optional<std::wstring> extractTrailingTrackSuffix(const std::filesystem::path &p)
	{
		std::wstring stem = p.stem().wstring();
		std::size_t hashPos = stem.rfind(L'#');
		if (hashPos != std::wstring::npos && hashPos + 1 < stem.size())
		{
			std::wstring digits = stem.substr(hashPos + 1);
			if (std::ranges::all_of(digits, [](wchar_t c) { return std::iswdigit(c) != 0; }))
			{
				return stem.substr(hashPos); // e.g. L"#18"
			}
		}
		return std::nullopt;
	}

	/// @brief Resolve a Windows .lnk shortcut file to its target file or folder path.
	/// Also propagates an optional #<trackNumber> suffix from the .lnk filename (e.g. "Rockman2#18.lnk").
	std::optional<std::filesystem::path> resolveShortcutLink(const std::filesystem::path &lnkPath)
	{
		HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		bool needCoUninit = SUCCEEDED(hrCo);

		struct CoGuard
		{
			bool active;
			~CoGuard()
			{
				if (active)
				{
					CoUninitialize();
				}
			}
		} coGuard{needCoUninit};

		IShellLinkW *psl = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
		if (FAILED(hr) || !psl)
		{
			return std::nullopt;
		}

		IPersistFile *ppf = nullptr;
		hr = psl->QueryInterface(IID_PPV_ARGS(&ppf));
		if (FAILED(hr) || !ppf)
		{
			psl->Release();
			return std::nullopt;
		}

		hr = ppf->Load(lnkPath.c_str(), STGM_READ);
		if (FAILED(hr))
		{
			ppf->Release();
			psl->Release();
			return std::nullopt;
		}

		wchar_t targetBuf[MAX_PATH * 4]{};
		WIN32_FIND_DATAW wfd{};
		hr = psl->GetPath(targetBuf, static_cast<int>(std::size(targetBuf)), &wfd, 0);

		std::error_code ec;
		if (FAILED(hr) || targetBuf[0] == L'\0' || !std::filesystem::exists(targetBuf, ec))
		{
			// Silent non-UI resolve attempt (handles moved relative shortcuts without modal dialogs)
			if (SUCCEEDED(psl->Resolve(nullptr, SLR_NO_UI | SLR_NOSEARCH | SLR_NOUPDATE)))
			{
				psl->GetPath(targetBuf, static_cast<int>(std::size(targetBuf)), &wfd, 0);
			}
		}

		ppf->Release();
		psl->Release();

		if (targetBuf[0] == L'\0')
		{
			return std::nullopt;
		}

		std::filesystem::path resolved(targetBuf);
		if (!std::filesystem::exists(resolved, ec))
		{
			return std::nullopt;
		}

		// If the .lnk name has a #<track> suffix (e.g. "Boss#18.lnk" or "MM2.nsf - 快捷方式#18.lnk")
		// and points to a file without #<track>, attach #<track> before the target extension.
		if (std::filesystem::is_regular_file(resolved, ec))
		{
			if (auto lnkTrackSuffix = extractTrailingTrackSuffix(lnkPath);
				lnkTrackSuffix.has_value() && !extractTrailingTrackSuffix(resolved).has_value())
			{
				resolved = resolved.parent_path() / (resolved.stem().wstring() + *lnkTrackSuffix + resolved.extension().wstring());
			}
		}

		return resolved;
	}

	void parsePlaylistFile(const std::filesystem::path &listPath, std::vector<std::filesystem::path> &results)
	{
		std::ifstream listFile(listPath);
		std::string line;
		bool firstLine = true;
		while (std::getline(listFile, line))
		{
			std::string_view sv(line);
			if (firstLine)
			{
				firstLine = false;
				// Strip UTF-8 BOM if saved by Windows Notepad
				if (sv.size() >= 3 &&
					static_cast<unsigned char>(sv[0]) == 0xEF &&
					static_cast<unsigned char>(sv[1]) == 0xBB &&
					static_cast<unsigned char>(sv[2]) == 0xBF)
				{
					sv.remove_prefix(3);
				}
			}

			std::string_view trimmed = trimAscii(sv);
			if (trimmed.empty() || trimmed.starts_with('#') || trimmed.starts_with("//"))
			{
				continue;
			}

			// Support both "song#3.nsf" and "song.nsf#3" in .m3u files
			std::string entryStr(trimmed);
			std::size_t hashAfterExt = entryStr.rfind('#');
			std::size_t dotPos = entryStr.rfind('.');
			if (hashAfterExt != std::string::npos && dotPos != std::string::npos && hashAfterExt > dotPos)
			{
				std::string trackSuffix = entryStr.substr(hashAfterExt); // "#3"
				std::string extPart = entryStr.substr(dotPos, hashAfterExt - dotPos);
				entryStr = entryStr.substr(0, dotPos) + trackSuffix + extPart;
			}

			std::filesystem::path itemPath = utf8ToPath(entryStr);
			if (itemPath.is_relative())
			{
				itemPath = listPath.parent_path() / itemPath;
			}
			if (extLowerAscii(itemPath) == ".lnk")
			{
				if (auto target = resolveShortcutLink(itemPath); target.has_value())
				{
					itemPath = *target;
				}
			}
			if (CustomAudioPlayer::isSupportedAudioFile(itemPath))
			{
				results.push_back(itemPath);
			}
		}
	}

	void scanFolderInto(
		const std::filesystem::path &folderPath,
		std::vector<std::filesystem::path> &results,
		std::unordered_set<std::wstring> &visitedDirs,
		int depth)
	{
		if (depth > 4)
		{
			return;
		}

		std::error_code ec;
		if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec))
		{
			return;
		}

		std::filesystem::path canonicalDir = std::filesystem::weakly_canonical(folderPath, ec);
		std::wstring dirKey = ec ? folderPath.wstring() : canonicalDir.wstring();
		if (!visitedDirs.insert(dirKey).second)
		{
			return;
		}

		constexpr auto iterOpts =
			std::filesystem::directory_options::skip_permission_denied |
			std::filesystem::directory_options::follow_directory_symlink;

		for (const auto &entry : std::filesystem::recursive_directory_iterator(folderPath, iterOpts, ec))
		{
			if (!entry.is_regular_file(ec))
			{
				continue;
			}

			const std::filesystem::path &p = entry.path();
			if (CustomAudioPlayer::isSupportedAudioFile(p))
			{
				results.push_back(p);
				continue;
			}

			std::string ext = extLowerAscii(p);
			if (ext == ".m3u" || ext == ".m3u8")
			{
				parsePlaylistFile(p, results);
			}
			else if (ext == ".lnk")
			{
				if (auto target = resolveShortcutLink(p); target.has_value())
				{
					if (std::filesystem::is_directory(*target, ec))
					{
						scanFolderInto(*target, results, visitedDirs, depth + 1);
					}
					else if (CustomAudioPlayer::isSupportedAudioFile(*target))
					{
						results.push_back(*target);
					}
					else
					{
						std::string targetExt = extLowerAscii(*target);
						if (targetExt == ".m3u" || targetExt == ".m3u8")
						{
							parsePlaylistFile(*target, results);
						}
					}
				}
			}
		}
	}
} // namespace

CustomMusicLibrary &CustomMusicLibrary::instance()
{
	static CustomMusicLibrary s_instance;
	return s_instance;
}

CustomMusicLibrary::CustomMusicLibrary()
	: m_rng(std::random_device{}())
{
}

std::filesystem::path CustomMusicLibrary::getBaseCustomMusicDir()
{
	return std::filesystem::path(CONFIG_FILE("custom_music"));
}

std::filesystem::path CustomMusicLibrary::getCategoryDir(std::string_view categoryName)
{
	return getBaseCustomMusicDir() / categoryName;
}

void CustomMusicLibrary::initDirectories()
{
	std::error_code ec;
	std::filesystem::path baseDir = getBaseCustomMusicDir();
	std::filesystem::create_directories(baseDir, ec);

	for (std::string_view cat : ALL_CATEGORIES)
	{
		std::filesystem::create_directories(baseDir / cat, ec);
	}

	// Generate README.txt if missing
	std::filesystem::path readmePath = baseDir / "README.txt";
	if (!std::filesystem::exists(readmePath, ec))
	{
		std::ofstream readme(readmePath, std::ios::out | std::ios::trunc);
		if (readme)
		{
			readme << "=== MMSFLC SoundMod - Custom Music Guide / 流星洛克人合集 自定义音乐说明 ===\n\n"
				   << "[中文说明]\n"
				   << "1. 将你的音频文件（或 Windows .lnk 快捷方式）放入对应场景的子文件夹中（如 normal_battle、boss_battle、real_world 等）。\n"
				   << "2. 在游戏的【声音设置 (Audio Settings)】中，将对应场景的选项设为：\n"
				   << "   - 【自定义音乐 (Custom Music)】：仅随机播放该文件夹中的外部音乐（按确认键可直接打开对应文件夹！）\n"
				   << "   - 【自定义 + 播放列表 (Custom + Playlist)】：将外部音乐与游戏内置自定义播放列表混合随机播放\n"
				   << "   - 若将场景设为【最爱 (Favorites)】，并且 favorites/ 文件夹中有音乐，也会自动混合进最爱随机池中。\n"
				   << "3. 支持的音频格式：\n"
				   << "   - 常规音频：.ogg, .mp3, .wav (支持内嵌 smpl 循环块), .flac\n"
				   << "   - 复古芯片音乐：.nsf, .nsfe, .spc, .vgm, .vgz, .gbs, .hes, .ay, .kss, .sap, .gym\n"
				   << "   - 快捷方式与列表：.lnk (支持指向单文件、文件夹或 .m3u), .m3u, .m3u8\n"
				   << "4. OGG 无缝循环支持：\n"
				   << "   - 自动识别 Vorbis Comments 中的循环标签（大小写及下划线不敏感）：\n"
				   << "     起点：LOOPSTART / LOOP_START / LOOPPOINT / LOOP_POINT / LOOP\n"
				   << "     长度或终点：LOOPLENGTH / LOOP_LENGTH / LOOPEND / LOOP_END\n"
				   << "   - 数值支持采样帧数（如 123456）、秒（如 12.5s）、毫秒（如 12500ms）或时间戳（如 01:23.456）。\n"
				   << "5. 零拷贝跨目录共享 (.lnk 快捷方式) 与多曲目 NSF / GBS 选曲：\n"
				   << "   - 按住 Alt 将任意音频文件或整个音乐文件夹拖入分类目录创建 .lnk 快捷方式即可直接调用，无需重复复制大文件。\n"
				   << "   - 多曲目文件选曲（十进制，从 1 开始）：\n"
				   << "     * 直接命名文件：`MM2#3.nsf`（播放第 3 首）\n"
				   << "     * 在 .lnk 快捷方式名末尾加 `#曲目号`：`MM2.nsf - 快捷方式#18.lnk` 或 `Boss#18.lnk`（同一 .nsf 跨目录选不同曲目）\n"
				   << "     * 在 .m3u 列表中按行填写：`../MegaMan.nsf#5`\n"
				   << "6. 单曲精确替换与全局微调 (overrides.json)：\n"
				   << "   - 可在 overrides.json 中按十六进制 TriggerID 绑定单首特定音乐替换，或通过可选字段 \"volume_scale\" (默认 1.0) 微调外部音乐整体增益。\n\n"
				   << "[English Guide]\n"
				   << "1. Drop audio files (or Windows .lnk shortcuts) into the category folders (normal_battle, boss_battle, real_world, etc.).\n"
				   << "2. In the in-game Audio Settings, choose 'Custom Music' (press Confirm to open the folder directly!)\n"
				   << "   or 'Custom + Playlist' to mix custom files with your in-game playlist.\n"
				   << "3. Supported formats: .ogg (with LOOPSTART/LOOP_START & LOOPLENGTH/LOOP_END), .mp3, .wav (smpl loop),\n"
				   << "   .flac, .nsf, .nsfe, .spc, .vgm, .vgz, .gbs, .hes, .ay, .kss, .sap, .gym, .lnk, .m3u/.m3u8.\n"
				   << "4. Zero-copy sharing & multi-track .nsf/.gbs selection (1-based decimal):\n"
				   << "   - Hold Alt + drag files or folders to create .lnk shortcuts across category folders without duplicating files.\n"
				   << "   - Append `#TrackNumber` to either the audio filename (`MM2#3.nsf`), a shortcut (`Boss#18.lnk`), or `.m3u` entries (`../MM2.nsf#5`).\n";
		}
	}

	// Generate overrides.json template if missing
	std::filesystem::path overridesPath = baseDir / "overrides.json";
	if (!std::filesystem::exists(overridesPath, ec))
	{
		std::ofstream ov(overridesPath, std::ios::out | std::ios::trunc);
		if (ov)
		{
			ov << "{\n"
			   << "  \"_comment\": \"Map hex PlayTriggerID (e.g. 0xFCF8ECEB for SF1 Hometown) to a file path or array of file paths relative to custom_music/\",\n"
			   << "  \"overrides\": {\n"
			   << "  }\n"
			   << "}\n";
		}
	}
}

std::vector<std::filesystem::path> CustomMusicLibrary::scanTracks(const std::filesystem::path &folderPath) const
{
	std::vector<std::filesystem::path> results;
	std::unordered_set<std::wstring> visitedDirs;
	scanFolderInto(folderPath, results, visitedDirs, 0);
	std::ranges::sort(results);
	return results;
}

std::optional<std::filesystem::path> CustomMusicLibrary::pickRandomFromList(
	const std::string &historyKey,
	const std::vector<std::filesystem::path> &tracks)
{
	if (tracks.empty())
	{
		return std::nullopt;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	if (tracks.size() == 1)
	{
		m_lastPickedByFolder[historyKey] = tracks[0];
		return tracks[0];
	}

	auto lastIt = m_lastPickedByFolder.find(historyKey);
	std::uniform_int_distribution<std::size_t> dist(0, tracks.size() - 1);
	std::size_t chosenIdx = dist(m_rng);

	if (lastIt != m_lastPickedByFolder.end() && tracks[chosenIdx] == lastIt->second)
	{
		chosenIdx = (chosenIdx + 1 + (dist(m_rng) % (tracks.size() - 1))) % tracks.size();
	}

	m_lastPickedByFolder[historyKey] = tracks[chosenIdx];
	return tracks[chosenIdx];
}

std::optional<std::filesystem::path> CustomMusicLibrary::pickRandomTrack(const std::filesystem::path &folderPath)
{
	std::vector<std::filesystem::path> tracks = scanTracks(folderPath);
	return pickRandomFromList(folderPath.string(), tracks);
}

void CustomMusicLibrary::reloadOverridesIfNeeded()
{
	std::error_code ec;
	std::filesystem::path overridesPath = getBaseCustomMusicDir() / "overrides.json";
	if (!std::filesystem::exists(overridesPath, ec))
	{
		m_triggerOverrides.clear();
		return;
	}

	auto writeTime = std::filesystem::last_write_time(overridesPath, ec);
	if (ec || (m_overridesLoadedOnce && writeTime == m_overridesLastWriteTime))
	{
		return;
	}

	m_overridesLastWriteTime = writeTime;
	m_overridesLoadedOnce = true;
	m_triggerOverrides.clear();

	try
	{
		std::ifstream file(overridesPath);
		if (!file)
		{
			return;
		}
		nlohmann::json root = nlohmann::json::parse(file, nullptr, true, true);
		if (root.is_object() && root.contains("volume_scale") && root["volume_scale"].is_number())
		{
			m_volumeScale.store(std::clamp(root["volume_scale"].get<float>(), 0.0f, 4.0f), std::memory_order_relaxed);
		}
		else
		{
			m_volumeScale.store(1.0f, std::memory_order_relaxed);
		}

		const nlohmann::json &mapNode = root.contains("overrides") && root["overrides"].is_object() ? root["overrides"] : root;
		if (!mapNode.is_object())
		{
			return;
		}

		std::filesystem::path baseDir = getBaseCustomMusicDir();
		for (auto &[key, val] : mapNode.items())
		{
			if (key.starts_with('_'))
			{
				continue;
			}
			std::uint32_t triggerId = 0;
			try
			{
				triggerId = static_cast<std::uint32_t>(std::stoul(key, nullptr, 0));
			}
			catch (...)
			{
				continue;
			}

			std::vector<std::filesystem::path> paths;
			auto addPathStr = [&](const std::string &s)
			{
				if (s.empty())
					return;
				std::filesystem::path p = utf8ToPath(s);
				if (p.is_relative())
				{
					p = baseDir / p;
				}
				if (extLowerAscii(p) == ".lnk")
				{
					if (auto resolved = resolveShortcutLink(p); resolved.has_value())
					{
						p = *resolved;
					}
				}
				if (std::filesystem::is_directory(p, ec))
				{
					auto dirTracks = scanTracks(p);
					paths.insert(paths.end(), dirTracks.begin(), dirTracks.end());
				}
				else if (CustomAudioPlayer::isSupportedAudioFile(p))
				{
					paths.push_back(p);
				}
			};

			if (val.is_string())
			{
				addPathStr(val.get<std::string>());
			}
			else if (val.is_array())
			{
				for (const auto &elem : val)
				{
					if (elem.is_string())
					{
						addPathStr(elem.get<std::string>());
					}
				}
			}

			if (!paths.empty())
			{
				m_triggerOverrides[triggerId] = std::move(paths);
			}
		}
	}
	catch (...)
	{
	}
}

std::optional<std::filesystem::path> CustomMusicLibrary::findOverrideForTrigger(std::uint32_t triggerId)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		reloadOverridesIfNeeded();
		auto it = m_triggerOverrides.find(triggerId);
		if (it == m_triggerOverrides.end() || it->second.empty())
		{
			return std::nullopt;
		}
		std::vector<std::filesystem::path> candidates = it->second;
		// Release lock handled below or pick inline:
		if (candidates.size() == 1)
		{
			return candidates[0];
		}
		std::uniform_int_distribution<std::size_t> dist(0, candidates.size() - 1);
		return candidates[dist(m_rng)];
	}
}

float CustomMusicLibrary::getVolumeScale()
{
	auto now = std::chrono::steady_clock::now();
	if (!m_overridesLoadedOnce || (now - m_lastOverridesCheck) >= std::chrono::seconds(2))
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_overridesLoadedOnce || (now - m_lastOverridesCheck) >= std::chrono::seconds(2))
		{
			m_lastOverridesCheck = now;
			reloadOverridesIfNeeded();
		}
	}
	return m_volumeScale.load(std::memory_order_relaxed);
}

bool CustomMusicLibrary::openFolderInExplorer(const std::filesystem::path &folderPath)
{
	std::error_code ec;
	std::filesystem::create_directories(folderPath, ec);
	std::filesystem::path absPath = std::filesystem::absolute(folderPath, ec);
	if (ec)
	{
		absPath = folderPath;
	}

	HINSTANCE res = ShellExecuteW(nullptr, L"open", absPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return reinterpret_cast<intptr_t>(res) > 32;
}
