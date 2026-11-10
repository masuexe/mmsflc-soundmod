#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

class CustomMusicLibrary
{
public:
	static CustomMusicLibrary &instance();

	/// @brief Base directory: reframework/data/ExpandedMusicOptions/custom_music
	static std::filesystem::path getBaseCustomMusicDir();

	/// @brief Get category folder path: reframework/data/ExpandedMusicOptions/custom_music/<categoryName>
	static std::filesystem::path getCategoryDir(std::string_view categoryName);

	/// @brief Create all standard category directories, README.txt, and overrides.json template
	void initDirectories();

	/// @brief Scan a category folder for all supported audio files and .m3u/.txt virtual track entries
	std::vector<std::filesystem::path> scanTracks(const std::filesystem::path &folderPath) const;

	/// @brief Pick a random track from a pre-scanned list, avoiding consecutive repeats for the given key
	std::optional<std::filesystem::path> pickRandomFromList(const std::string &historyKey, const std::vector<std::filesystem::path> &tracks);

	/// @brief Scan and pick a random track from a category folder
	std::optional<std::filesystem::path> pickRandomTrack(const std::filesystem::path &folderPath);

	/// @brief Check overrides.json for a specific Wwise PlayTriggerID override
	std::optional<std::filesystem::path> findOverrideForTrigger(std::uint32_t triggerId);

	/// @brief Optional user volume multiplier from overrides.json ("volume_scale", default 1.0f)
	float getVolumeScale();

	/// @brief Open a category folder in Windows File Explorer so the user can drop/upload audio files
	static bool openFolderInExplorer(const std::filesystem::path &folderPath);

private:
	CustomMusicLibrary();

	void reloadOverridesIfNeeded();

	mutable std::mutex m_mutex;
	std::mt19937 m_rng;
	std::unordered_map<std::string, std::filesystem::path> m_lastPickedByFolder;

	std::filesystem::file_time_type m_overridesLastWriteTime{};
	std::chrono::steady_clock::time_point m_lastOverridesCheck{};
	bool m_overridesLoadedOnce = false;
	std::atomic<float> m_volumeScale{1.0f};
	std::unordered_map<std::uint32_t, std::vector<std::filesystem::path>> m_triggerOverrides;
};
