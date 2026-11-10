#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct ma_engine;
struct ma_sound;

class CustomAudioPlayer
{
public:
	struct LoopInfo
	{
		bool hasLoopTag = false;
		std::uint64_t loopStartFrame = 0;
		std::uint64_t loopEndFrame = 0; // 0 means end of stream
		std::uint64_t totalFrames = 0;
		std::uint32_t sampleRate = 0;
	};

	struct TrackMetadata
	{
		std::filesystem::path filePath;
		std::string format;
		int subTrackIndex = 0; // For multi-track chiptunes like .nsf
		LoopInfo loopInfo;
	};

	static CustomAudioPlayer &instance();

	bool init();
	void shutdown();

	/// @brief Check if file extension is a supported audio format (.mp3, .ogg, .wav, .flac, .nsf, .nsfe, .spc, .vgm, .vgz, .gbs, .hes, .ay, .kss, .sap, .gym)
	static bool isSupportedAudioFile(const std::filesystem::path &path);

	/// @brief Inspect OGG/WAV loop metadata without starting playback
	static std::optional<LoopInfo> inspectLoopInfo(const std::filesystem::path &path);

	/// @brief Play an external audio file on a given logical player slot (default 0 for main BGM, 1 for sub/launcher/music player)
	/// @param filePath Path to audio file (supports optional #N suffix before extension for multi-track NSF/GBS, e.g. "mm2#3.nsf")
	/// @param loop Whether the track should loop
	/// @param playerSlot Logical slot index
	/// @return true if playback started successfully
	bool play(const std::filesystem::path &filePath, bool loop = true, std::size_t playerSlot = 0);

	/// @brief Stop playback on a specific slot
	void stop(std::size_t playerSlot = 0);

	/// @brief Stop all active custom audio slots
	void stopAll();

	/// @brief Fade out and stop playback over durationMs milliseconds
	void fadeOutAndStop(std::uint32_t durationMs = 500, std::size_t playerSlot = 0);

	/// @brief Pause playback on a specific slot
	void pause(std::size_t playerSlot = 0);

	/// @brief Resume playback on a specific slot
	void resume(std::size_t playerSlot = 0);

	/// @brief Check if a slot is currently playing custom audio
	bool isPlaying(std::size_t playerSlot = 0) const;

	/// @brief Check if a slot is currently paused
	bool isPaused(std::size_t playerSlot = 0) const;

	/// @brief Set master volume (0.0f to 1.0f), synced with in-game Master * BGM volume
	void setMasterVolume(float volume);
	float getMasterVolume() const;

	/// @brief Periodic update (handles fade-out completion and game volume sync)
	void update();

	/// @brief Get info about the currently loaded track on a slot
	std::optional<TrackMetadata> getCurrentTrack(std::size_t playerSlot = 0) const;

private:
	CustomAudioPlayer() = default;
	~CustomAudioPlayer();

	CustomAudioPlayer(const CustomAudioPlayer &) = delete;
	CustomAudioPlayer &operator=(const CustomAudioPlayer &) = delete;

	struct SlotState;

	mutable std::recursive_mutex m_mutex;
	bool m_initialized = false;
	ma_engine *m_engine = nullptr;
	float m_masterVolume = 1.0f;
	std::vector<std::unique_ptr<SlotState>> m_slots;
};
