#include <Enums_Internal.hpp>

#include <format>
#include <memory>
#include <unordered_set>

#include "AudioMenuExtension.hpp"
#include "Common.hpp"
#include "CustomAudioPlayer.hpp"
#include "CustomMusicLibrary.hpp"
#include "CustomPlaylistMenuItem.hpp"
#include "MusicSystemMod.hpp"
#include "REFrameworkHelper.hpp"

using namespace REFrameworkHelper;

namespace
{
	bool isOneShotJingleTrigger(std::uint32_t triggerId)
	{
		static const std::unordered_set<std::uint32_t> ONE_SHOT_TRIGGERS = {
			0xA5C48DF3, // SF1: Winner! - Short
			0x24628ADA, // SF2: Winner! - Short (SF1 Version)
			0xBD886180, // SF2: Winner! - Short
			0xF37BEB7B, // SF3: Winner! - Short
			0x8489A114, // SF1: Wave Out!
			0x0C4E4C2D, // SF2: Wave Out!
		};
		return ONE_SHOT_TRIGGERS.contains(triggerId);
	}
}

/// @brief Install all hooks used for music system mod
void MusicSystemMod::installHooks()
{
	// Override corePlayBgm
	static std::optional<std::uint8_t> s_corePlayBgm_retVal;
	s_hooks.emplace_back(hook(
		"app.cSound_Base.corePlayBgm",
		[](int argc, void **argv, auto...)
		{
			s_corePlayBgm_retVal.reset();

			Object sound = Object(argv[1]);
			std::uint32_t triggerId = (std::uint32_t)(intptr_t)argv[2];
			Object origId2TriggerId = Object(argv[3]);
			std::uint32_t playerId = (std::uint32_t)(intptr_t)argv[4];

			bool isEmptyPreferredMix = false;
			bool startedCustomAudio = false;
			bool shouldLoop = !isOneShotJingleTrigger(triggerId);
			bool isArranged = sound.get<std::uint8_t>("_CurrentBgmPlayType") == getType("app.cSound_Base").get<std::uint8_t>("BgmPlayType_Arrange");

			auto playCustomTrack = [&](const std::filesystem::path &trackPath) -> std::optional<std::uint8_t>
			{
				std::uint32_t stopTriggerId = sound["_BgmSetting"].get<std::uint32_t>("DefaultStopTriggerId");
				s_internalStopCall = true;
				sound.call("coreStopBgm", stopTriggerId, playerId);
				s_internalStopCall = false;

				MusicSystemMod::syncCustomAudioVolume();
				if (!CustomAudioPlayer::instance().play(trackPath, shouldLoop, playerId))
				{
					reframework::API::get()->log_warn(
						std::format("[ExpandedMusicOptions] Failed to play custom track: {}", trackPath.string()).c_str());
					return {};
				}

				if (auto meta = CustomAudioPlayer::instance().getCurrentTrack(playerId); meta.has_value())
				{
					reframework::API::get()->log_info(
						std::format(
							"[ExpandedMusicOptions] Playing custom track (slot={}, format={}, loop={}, hasLoopTag={}, loopStart={}, loopEnd={}): {}",
							playerId,
							meta->format,
							shouldLoop,
							meta->loopInfo.hasLoopTag,
							meta->loopInfo.loopStartFrame,
							meta->loopInfo.loopEndFrame,
							trackPath.filename().string())
							.c_str());
				}

				startedCustomAudio = true;
				s_isPlayerCustomAudio[playerId] = true;

				Object playingBgmInfo = sound["_PlayingBgmInfoList"][playerId];
				std::uint8_t favPlayType = getType("app.cSound_Base").get<std::uint8_t>("BgmPlayType_Favorite");
				playingBgmInfo.set<std::uint8_t>("PlayType", favPlayType);

				return favPlayType;
			};

			auto selectNoChange = [&](MusicSettingInfo &musicSettingInfo) -> std::optional<std::uint8_t>
			{
				return {};
			};
			auto selectMusicOff = [&](MusicSettingInfo &musicSettingInfo) -> std::optional<std::uint8_t>
			{
				std::uint32_t stopTriggerId = sound["_BgmSetting"].get<std::uint32_t>("DefaultStopTriggerId");

				sound.call("coreStopBgm", stopTriggerId, playerId);

				return 0;
			};
			auto selectCustomMusic = [&](MusicSettingInfo &musicSettingInfo) -> std::optional<std::uint8_t>
			{
				std::shared_ptr<CustomPlaylistMenuItem> playlistMenuItem = std::dynamic_pointer_cast<CustomPlaylistMenuItem>(musicSettingInfo.MenuItem);
				if (!playlistMenuItem || !playlistMenuItem->getCustomMusicFolder().has_value())
				{
					return {};
				}

				auto track = CustomMusicLibrary::instance().pickRandomTrack(*playlistMenuItem->getCustomMusicFolder());
				if (!track.has_value())
				{
					return {};
				}

				return playCustomTrack(*track);
			};
			auto selectAlwaysOriginal = [&](MusicSettingInfo &musicSettingInfo) -> std::optional<std::uint8_t>
			{
				Object playingBgmInfo = sound["_PlayingBgmInfoList"][playerId];
				Object playerObject = playingBgmInfo["PlayerObject"];

				MusicSystemMod::setEnableBgmArrangeForced(false);

				sound["_ResidentContainer"].call(
					"trigger(System.UInt32, via.GameObject, via.GameObject, System.UInt32, System.Boolean, System.UInt32, via.simplewwise.CallbackType, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>)",
					triggerId,
					playerObject,
					nullptr,
					0xFFFFFFFF,
					false,
					0,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr);
				playingBgmInfo.set<bool>("IsPlayOriginalContainer", true);
				playingBgmInfo.set<bool>("PlayType", 0);

				return getType("app.cSound_Base").get<std::uint8_t>("BgmPlayType_Original");
			};
			auto selectAlwaysArranged = [&](MusicSettingInfo &musicSettingInfo) -> std::optional<std::uint8_t>
			{
				Object playingBgmInfo = sound["_PlayingBgmInfoList"][playerId];
				Object playerObject = playingBgmInfo["PlayerObject"];

				MusicSystemMod::setEnableBgmArrangeForced(true);

				sound["_ResidentContainer"].call(
					"trigger(System.UInt32, via.GameObject, via.GameObject, System.UInt32, System.Boolean, System.UInt32, via.simplewwise.CallbackType, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>)",
					triggerId,
					playerObject,
					nullptr,
					0xFFFFFFFF,
					false,
					0,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr);
				playingBgmInfo.set<bool>("IsPlayOriginalContainer", true);
				playingBgmInfo.set<bool>("PlayType", 0);

				return getType("app.cSound_Base").get<std::uint8_t>("BgmPlayType_Original");
			};
			auto selectMix = [&](MusicSettingInfo &musicSettingInfo) -> std::optional<std::uint8_t>
			{
				bool isPreferredMix = (CustomPlaylistMenuItem::Option)musicSettingInfo.MenuItem->getValue() == CustomPlaylistMenuItem::Option::PreferredMix;

				// This will internally loop through the music settings info list again...
				std::optional<bool> mixOverride = MusicSystemMod::getOverrideMixForTriggerID(triggerId);

				// We always need to set EnableBgmArrange appropriately here
				if (mixOverride.has_value())
				{
					isArranged = mixOverride.value();
				}

				if (isPreferredMix && !mixOverride.has_value())
				{
					// We don't want this player to be overwritten; it should follow the BGM Selection setting
					isEmptyPreferredMix = true;
				}

				Object playingBgmInfo = sound["_PlayingBgmInfoList"][playerId];
				Object playerObject = playingBgmInfo["PlayerObject"];
				sound["_ResidentContainer"].call(
					"trigger(System.UInt32, via.GameObject, via.GameObject, System.UInt32, System.Boolean, System.UInt32, via.simplewwise.CallbackType, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>)",
					triggerId,
					playerObject,
					nullptr,
					0xFFFFFFFF,
					false,
					0,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr);
				playingBgmInfo.set<bool>("IsPlayOriginalContainer", true);
				playingBgmInfo.set<bool>("PlayType", 0);

				return getType("app.cSound_Base").get<std::uint8_t>("BgmPlayType_Original");
			};
			auto selectPlaylist = [&](MusicSettingInfo &musicSettingInfo) -> std::optional<std::uint8_t>
			{
				CustomPlaylistMenuItem::Option opt = (CustomPlaylistMenuItem::Option)musicSettingInfo.MenuItem->getValue();
				std::shared_ptr<CustomPlaylistMenuItem> playlistMenuItem = std::dynamic_pointer_cast<CustomPlaylistMenuItem>(musicSettingInfo.MenuItem);

				std::vector<std::filesystem::path> customTracks;
				std::string historyKey;
				Object playlist;

				if (opt == CustomPlaylistMenuItem::Option::CustomAndPlaylist)
				{
					if (playlistMenuItem)
					{
						if (playlistMenuItem->getCustomMusicFolder().has_value())
						{
							customTracks = CustomMusicLibrary::instance().scanTracks(*playlistMenuItem->getCustomMusicFolder());
							historyKey = playlistMenuItem->getCustomMusicFolder()->string();
						}
						playlist = playlistMenuItem->getCustomPlaylist();
					}
				}
				else if (opt == CustomPlaylistMenuItem::Option::Playlist)
				{
					if (playlistMenuItem)
					{
						playlist = playlistMenuItem->getCustomPlaylist();
					}
				}
				else // Option::Favorites
				{
					std::filesystem::path favDir = CustomMusicLibrary::getCategoryDir("favorites");
					customTracks = CustomMusicLibrary::instance().scanTracks(favDir);
					historyKey = favDir.string();
					playlist = getSingleton("app.Launcher")["_saveData"]["favoriteMusicList"];
				}

				std::int32_t customCount = static_cast<std::int32_t>(customTracks.size());
				std::int32_t playlistSize = (playlist != nullptr) ? playlist.get<std::int32_t>("Count") : 0;
				std::int32_t totalCount = customCount + playlistSize;
				if (totalCount <= 0)
				{
					return {};
				}

				std::int32_t r = getType("via.MathEx").call<std::int32_t>("randomRangeIndex", 0, totalCount);
				if (r < customCount)
				{
					if (auto picked = CustomMusicLibrary::instance().pickRandomFromList(historyKey, customTracks); picked.has_value())
					{
						if (auto res = playCustomTrack(*picked); res.has_value())
						{
							return res;
						}
					}
					if (playlistSize <= 0)
					{
						return {};
					}
					r = customCount + getType("via.MathEx").call<std::int32_t>("randomRangeIndex", 0, playlistSize);
				}

				Object musicInfo = playlist[r - customCount];
				std::uint16_t musicId = musicInfo.get<std::uint16_t>("musicId");
				isArranged = musicInfo.get<bool>("isArrange");

				Object playingBgmInfo = sound["_PlayingBgmInfoList"][playerId];
				Object playerObject = playingBgmInfo["PlayerObject"];
				if (!MusicSystemMod::playBgm(playerObject, musicId, isArranged))
				{
					return {};
				}

				Type soundMilkyDefine = getType("app.sound.SoundMilkyDefine");
				std::uint8_t series = soundMilkyDefine.call<std::uint8_t>("getMusicPlayerBgmDefine_Series", musicId);
				std::uint8_t bgmSettingIndex = soundMilkyDefine.call<std::uint8_t>("getMusicPlayerBgmDefine_Index", musicId);
				playingBgmInfo.set<std::uint8_t>("FavoriteBgmId_Series", series);
				playingBgmInfo.set<std::uint8_t>("FavoriteBgmId_Index", bgmSettingIndex);
				playingBgmInfo.set<std::uint8_t>("PlayType", getType("app.cSound_Base").get<std::uint8_t>("BgmPlayType_Favorite"));

				return getType("app.cSound_Base").get<std::uint8_t>("BgmPlayType_Favorite");
			};
			auto selectDlcMusic = [&](MusicSettingInfo &musicSettingInfo) -> std::optional<std::uint8_t>
			{
				Object extraResource01 = sound
					["_Manager"]["_ExtraResourceList"]
					[app::sound::SoundMilkyManager::ExtraResourceTypeEnum::ExtraResource01];
				if (extraResource01 == nullptr || (Object)(extraResource01["GameObject"]) == nullptr)
				{
					return {};
				}
				Object container = extraResource01["Container"];
				Object playingBgmInfo = sound["_PlayingBgmInfoList"][playerId];
				Object playerObject = playingBgmInfo["PlayerObject"];

				container.call(
					"trigger(System.UInt32, via.GameObject, via.GameObject, System.UInt32, System.Boolean, System.UInt32, via.simplewwise.CallbackType, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>)",
					musicSettingInfo.NewTriggerID,
					playerObject,
					nullptr,
					0xFFFFFFFF,
					false,
					0,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr);

				return getType("app.cSound_Base").get<std::uint8_t>("BgmPlayType_Original");
			};

			// Check per-TriggerID custom override from overrides.json first
			if (auto overrideTrack = CustomMusicLibrary::instance().findOverrideForTrigger(triggerId); overrideTrack.has_value())
			{
				s_corePlayBgm_retVal = playCustomTrack(*overrideTrack);
			}

			if (!s_corePlayBgm_retVal.has_value())
			{
				Object dlcContentsManager = getSingleton("app.DLCContentsManager");
				for (MusicSettingInfo &musicSettingInfo : s_musicSettingInfoList)
				{
					if (musicSettingInfo.RequiredDLC != app::DLCContentsManager::DLC_TYPE::INVALID)
					{
						if (!dlcContentsManager.call<bool>("getHasDLC", musicSettingInfo.RequiredDLC))
						{
							continue;
						}
					}

					if (std::ranges::find(musicSettingInfo.TriggerIDs, triggerId) != musicSettingInfo.TriggerIDs.end())
					{
						switch (musicSettingInfo.Type)
						{
						case MusicSettingType::CustomPlaylist:
							switch ((CustomPlaylistMenuItem::Option)musicSettingInfo.MenuItem->getValue())
							{
							default:
								s_corePlayBgm_retVal = selectNoChange(musicSettingInfo);
								break;
							case CustomPlaylistMenuItem::Option::MusicOff:
								s_corePlayBgm_retVal = selectMusicOff(musicSettingInfo);
								break;
							case CustomPlaylistMenuItem::Option::AlwaysOriginal:
							case CustomPlaylistMenuItem::Option::AlwaysArranged:
							case CustomPlaylistMenuItem::Option::PreferredMix:
								s_corePlayBgm_retVal = selectMix(musicSettingInfo);
								break;
							case CustomPlaylistMenuItem::Option::Playlist:
							case CustomPlaylistMenuItem::Option::Favorites:
							case CustomPlaylistMenuItem::Option::CustomAndPlaylist:
								s_corePlayBgm_retVal = selectPlaylist(musicSettingInfo);
								break;
							case CustomPlaylistMenuItem::Option::CustomMusic:
								s_corePlayBgm_retVal = selectCustomMusic(musicSettingInfo);
								break;
							}
							break;
						case MusicSettingType::ReplaceMusic:
							if (musicSettingInfo.MenuItem->getValue() != 0)
							{
								s_corePlayBgm_retVal = selectDlcMusic(musicSettingInfo);
							}
							break;
						}
					}

					if (s_corePlayBgm_retVal.has_value())
					{
						break;
					}
				}
			}

			// If this player slot previously played custom audio and is now playing a non-custom BGM (or MusicOff), stop custom audio on this slot
			if (!startedCustomAudio && s_isPlayerCustomAudio[playerId])
			{
				CustomAudioPlayer::instance().stop(playerId);
				s_isPlayerCustomAudio[playerId] = false;
			}

			// For empty Preferred Mix, we want to have the player not be overridden (it should follow Arrange flag)
			// However we do want to block music restart, since we will play the original track over MMBN tracks
			MusicSystemMod::setEnableBgmArrangeForced(isArranged);
			s_playerPlayTriggerID[playerId] = triggerId;
			s_isPlayerOverridden[playerId] = isEmptyPreferredMix ? false : s_corePlayBgm_retVal.has_value();
			s_suppressUpdatePlayType[playerId] = s_corePlayBgm_retVal.has_value();
			return s_corePlayBgm_retVal.has_value() ? REFRAMEWORK_HOOK_SKIP_ORIGINAL : REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		[](void **retval, auto...)
		{
			if (s_corePlayBgm_retVal.has_value())
			{
				*retval = (void *)std::move(*s_corePlayBgm_retVal);
				s_corePlayBgm_retVal.reset();
			}
		}));

	s_hooks.emplace_back(hook(
		"app.cSound_Base.coreStopBgm",
		[](int argc, void **argv, auto...)
		{
			Object sound = Object(argv[1]);
			std::uint32_t stopTriggerId = (std::uint32_t)(intptr_t)argv[2];
			std::uint32_t playerId = (std::uint32_t)(intptr_t)argv[3];

			if (!s_internalStopCall && !s_inPauseCall && s_isPlayerCustomAudio[playerId])
			{
				std::uint32_t fadeOutTriggerId = sound["_BgmSetting"].get<std::uint32_t>("PlayingBgmFadeOutTriggerId");
				if (stopTriggerId == fadeOutTriggerId)
				{
					CustomAudioPlayer::instance().fadeOutAndStop(450, playerId);
				}
				else
				{
					CustomAudioPlayer::instance().stop(playerId);
				}
				s_isPlayerCustomAudio[playerId] = false;
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	static Object s_baseChangeBgm_sound;
	static std::uint32_t s_baseChangeBgm_playerId = 0;
	s_hooks.emplace_back(hook(
		"app.cSound_Base.baseChangeBgm",
		[](int argc, void **argv, auto...)
		{
			s_baseChangeBgm_sound = Object(argv[1]);
			s_baseChangeBgm_playerId = (std::uint32_t)(intptr_t)argv[4];
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		[](auto...)
		{
			if (s_baseChangeBgm_sound != nullptr && s_isPlayerCustomAudio[s_baseChangeBgm_playerId])
			{
				Object playingBgmInfo = s_baseChangeBgm_sound["_PlayingBgmInfoList"][s_baseChangeBgm_playerId];
				if (playingBgmInfo != nullptr)
				{
					auto bgmState = (app::cSound_Base::PlayingBgmStetaEnum)playingBgmInfo.get<std::int8_t>("State");
					if (bgmState == app::cSound_Base::PlayingBgmStetaEnum::None)
					{
						CustomAudioPlayer::instance().stop(s_baseChangeBgm_playerId);
						s_isPlayerCustomAudio[s_baseChangeBgm_playerId] = false;
					}
				}
			}
			s_baseChangeBgm_sound = Object();
		}));

	s_hooks.emplace_back(hook(
		"app.cSound_Base.basePauseBgm",
		[](int argc, void **argv, auto...)
		{
			std::uint32_t playerId = (std::uint32_t)(intptr_t)argv[2];
			s_inPauseCall = true;
			if (s_isPlayerCustomAudio[playerId])
			{
				CustomAudioPlayer::instance().pause(playerId);
			}
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		[](auto...)
		{
			s_inPauseCall = false;
		}));

	s_hooks.emplace_back(hook(
		"app.cSound_Base.baseResumeBgm",
		[](int argc, void **argv, auto...)
		{
			Object sound = Object(argv[1]);
			std::uint32_t playerId = (std::uint32_t)(intptr_t)argv[3];
			if (s_isPlayerCustomAudio[playerId])
			{
				Object playingBgmInfo = sound["_PlayingBgmInfoList"][playerId];
				if (playingBgmInfo != nullptr)
				{
					auto bgmState = (app::cSound_Base::PlayingBgmStetaEnum)playingBgmInfo.get<std::int8_t>("State");
					if (bgmState == app::cSound_Base::PlayingBgmStetaEnum::Pause)
					{
						CustomAudioPlayer::instance().resume(playerId);
					}
				}
			}
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.cSound_Base.baseTerminate",
		[](auto...)
		{
			MusicSystemMod::stopAllIngameCustomAudio();
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.cSound_Base.stopBgmGameOver",
		[](auto...)
		{
			MusicSystemMod::stopAllIngameCustomAudio();
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.sound.SoundMilkyManager.set_EnableBgmArrange(System.Boolean)",
		[](int argc, void **argv, auto...)
		{
			Object manager = Object(argv[1]);
			Object sound = manager["_IngameSoundSystem"];
			if (sound != nullptr)
			{
				// Call normally if we are in music player
				if (!getSingleton("app.Launcher").call<bool>("isMusicPlayerEnd"))
				{
					return REFRAMEWORK_HOOK_CALL_ORIGINAL;
				}

				std::uint32_t playerId = sound.get<std::uint32_t>("_CurrentPlayBgmPlayerId");
				if (s_disableEnableBgmArrangeHook)
				{
					bool isArranged = (std::uint8_t)(intptr_t)argv[2];
					s_isPlayerArranged[playerId] = isArranged;
				}
				else if (s_isPlayerOverridden[playerId])
				{
					return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
				}
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.cSound_Base.doUpdate",
		[](auto...)
		{
			// Skip ingame sound update if we are in music player
			if (!getSingleton("app.Launcher").call<bool>("isMusicPlayerEnd"))
			{
				return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	static std::vector<app::cSound_Base::PlayingBgmStetaEnum> updateBgmPlayType_playingBgmInfoStates;
	static Object updateBgmPlayType_sound;
	s_hooks.emplace_back(hook(
		"app.cSound_Base.updateBgmPlayType",
		[](int argc, void **argv, auto...)
		{
			Object sound = Object(argv[1]);
			updateBgmPlayType_sound = sound;

			std::uint32_t playerId = sound.get<std::uint32_t>("_CurrentPlayBgmPlayerId");
			if (s_isPlayerOverridden[playerId] || s_forceUpdatePlayTypeArrange[playerId])
			{
				// Set BGM arrange flag to what we know it to be
				MusicSystemMod::setEnableBgmArrangeForced(s_isPlayerArranged[playerId]);
				s_forceUpdatePlayTypeArrange[playerId] = false;
			}
			else
			{
				// Allow updateBgmPlayType to update Arranged flag
				s_disableEnableBgmArrangeHook = true;
			}

			// Set playing BGM state to None temporarily for our overridden players
			// This way they will be skipped by updateBgmPlayType
			Object playingBgmInfoList = sound["_PlayingBgmInfoList"];
			std::int32_t playingBgmInfoListLength = playingBgmInfoList.get<std::int32_t>("Length");
			updateBgmPlayType_playingBgmInfoStates.resize(playingBgmInfoListLength);
			for (std::int32_t i = 0; i < playingBgmInfoListLength; ++i)
			{
				Object playingBgmInfo = playingBgmInfoList[i];
				app::cSound_Base::PlayingBgmStetaEnum bgmState = (app::cSound_Base::PlayingBgmStetaEnum)playingBgmInfo.get<std::int8_t>("State");
				updateBgmPlayType_playingBgmInfoStates[i] = bgmState;

				if (s_suppressUpdatePlayType[i])
				{
					playingBgmInfo.set<std::int8_t>("State", app::cSound_Base::PlayingBgmStetaEnum::None);
				}
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		[](auto...)
		{
			Object sound = updateBgmPlayType_sound;

			s_disableEnableBgmArrangeHook = false;

			// Restore original playing BGM states
			Object playingBgmInfoList = sound["_PlayingBgmInfoList"];
			std::int32_t playingBgmInfoListLength = playingBgmInfoList.get<std::int32_t>("Length");
			for (std::int32_t i = 0; i < playingBgmInfoListLength; ++i)
			{
				Object playingBgmInfo = playingBgmInfoList[i];
				app::cSound_Base::PlayingBgmStetaEnum oldBgmState = updateBgmPlayType_playingBgmInfoStates[i];
				app::cSound_Base::PlayingBgmStetaEnum newBgmState = (app::cSound_Base::PlayingBgmStetaEnum)playingBgmInfo.get<std::int8_t>("State");
				if (newBgmState == app::cSound_Base::PlayingBgmStetaEnum::None)
				{
					playingBgmInfo.set<std::int8_t>("State", oldBgmState);
				}
			}
		}));

	static std::uint16_t launcherReplacedBgmId = getType("app.sound.SoundMilkyDefine").get<std::uint16_t>("INVALID_BGM_ID");
	static bool s_internalLauncherStop = false;
	s_hooks.emplace_back(hook(
		"app.sound.SoundLauncherBgmManager.update",
		[](int argc, void **argv, auto...)
		{
			Object soundLauncherBgmManager = Object(argv[1]);

			if (!soundLauncherBgmManager.get<bool>("_IsInit"))
			{
				launcherReplacedBgmId = getType("app.sound.SoundMilkyDefine").get<std::uint16_t>("INVALID_BGM_ID");
				if (s_isLauncherCustomAudio)
				{
					CustomAudioPlayer::instance().stop(LAUNCHER_CUSTOM_AUDIO_SLOT);
					s_isLauncherCustomAudio = false;
				}
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.sound.SoundLauncherBgmManager.playBgm",
		[](int argc, void **argv, auto...)
		{
			Object soundLauncherBgmManager = Object(argv[1]);
			std::uint16_t bgmId = (std::uint16_t)(intptr_t)argv[2];
			Object srcObj = Object(argv[3]);

			Type soundMilkyDefine = getType("app.sound.SoundMilkyDefine");
			bool startedLauncherCustomAudio = false;

			auto playLauncherCustomTrack = [&](const std::filesystem::path &trackPath) -> bool
			{
				std::uint16_t savedMainBgmId = soundLauncherBgmManager.get<std::uint16_t>("_CurrentMainBgmId");
				s_internalLauncherStop = true;
				soundLauncherBgmManager.call("stopMainBgm");
				s_internalLauncherStop = false;
				soundLauncherBgmManager.set<std::uint16_t>("_CurrentMainBgmId", savedMainBgmId != soundMilkyDefine.get<std::uint16_t>("INVALID_BGM_ID") ? savedMainBgmId : bgmId);

				MusicSystemMod::syncCustomAudioVolume();
				if (!CustomAudioPlayer::instance().play(trackPath, true, LAUNCHER_CUSTOM_AUDIO_SLOT))
				{
					reframework::API::get()->log_warn(
						std::format("[ExpandedMusicOptions] Failed to play Launcher custom track: {}", trackPath.string()).c_str());
					return false;
				}

				if (auto meta = CustomAudioPlayer::instance().getCurrentTrack(LAUNCHER_CUSTOM_AUDIO_SLOT); meta.has_value())
				{
					reframework::API::get()->log_info(
						std::format(
							"[ExpandedMusicOptions] Playing Launcher custom track (format={}, hasLoopTag={}, loopStart={}, loopEnd={}): {}",
							meta->format,
							meta->loopInfo.hasLoopTag,
							meta->loopInfo.loopStartFrame,
							meta->loopInfo.loopEndFrame,
							trackPath.filename().string())
							.c_str());
				}

				startedLauncherCustomAudio = true;
				s_isLauncherCustomAudio = true;
				launcherReplacedBgmId = soundMilkyDefine.get<std::uint16_t>("INVALID_BGM_ID");
				return true;
			};

			auto selectNoChange = [&](CustomPlaylistMenuItem::Option optionValue)
			{
				return false;
			};
			auto selectMusicOff = [&](CustomPlaylistMenuItem::Option optionValue)
			{
				s_internalLauncherStop = true;
				soundLauncherBgmManager.call("stopMainBgm");
				s_internalLauncherStop = false;

				Type bgmRequestType = getType("app.sound.SoundLauncherBgmManager.BgmRequest");
				std::uint64_t bgmRequestRaw = soundLauncherBgmManager.get<std::uint64_t>("_BgmRequest");
				*(std::uint16_t *)bgmRequestType.m_type->find_field("BgmId")->get_data_raw(&bgmRequestRaw, true) = soundMilkyDefine.get<std::uint16_t>("INVALID_BGM_ID");
				soundLauncherBgmManager.set<std::uint64_t>("_BgmRequest", bgmRequestRaw);

				launcherReplacedBgmId = soundMilkyDefine.get<std::uint16_t>("INVALID_BGM_ID");

				return true;
			};
			auto selectCustomMusic = [&](CustomPlaylistMenuItem::Option optionValue)
			{
				if (!s_menuItemMainMenuBgm || !s_menuItemMainMenuBgm->getCustomMusicFolder().has_value())
				{
					return false;
				}
				auto track = CustomMusicLibrary::instance().pickRandomTrack(*s_menuItemMainMenuBgm->getCustomMusicFolder());
				if (!track.has_value())
				{
					return false;
				}
				return playLauncherCustomTrack(*track);
			};
			auto selectPlaylist = [&](CustomPlaylistMenuItem::Option optionValue)
			{
				std::vector<std::filesystem::path> customTracks;
				std::string historyKey;
				if (optionValue == CustomPlaylistMenuItem::Option::CustomAndPlaylist &&
					s_menuItemMainMenuBgm &&
					s_menuItemMainMenuBgm->getCustomMusicFolder().has_value())
				{
					customTracks = CustomMusicLibrary::instance().scanTracks(*s_menuItemMainMenuBgm->getCustomMusicFolder());
					historyKey = s_menuItemMainMenuBgm->getCustomMusicFolder()->string();
				}
				else if (optionValue == CustomPlaylistMenuItem::Option::Favorites)
				{
					std::filesystem::path favDir = CustomMusicLibrary::getCategoryDir("favorites");
					customTracks = CustomMusicLibrary::instance().scanTracks(favDir);
					historyKey = favDir.string();
				}

				Object playlist;
				if (MusicSystemMod::hasMainMenuBgm())
				{
					if (optionValue == CustomPlaylistMenuItem::Option::Playlist ||
						optionValue == CustomPlaylistMenuItem::Option::CustomAndPlaylist)
					{
						playlist = s_menuItemMainMenuBgm->getCustomPlaylist();
					}
					else
					{
						playlist = getSingleton("app.Launcher")["_saveData"]["favoriteMusicList"];
					}
				}

				std::int32_t customCount = static_cast<std::int32_t>(customTracks.size());
				std::int32_t playlistSize = (playlist != nullptr) ? playlist.get<std::int32_t>("Count") : 0;
				std::int32_t totalCount = customCount + playlistSize;
				if (totalCount <= 0)
				{
					return false;
				}

				std::int32_t r = getType("via.MathEx").call<std::int32_t>("randomRangeIndex", 0, totalCount);
				if (r < customCount)
				{
					if (auto picked = CustomMusicLibrary::instance().pickRandomFromList(historyKey, customTracks); picked.has_value())
					{
						if (playLauncherCustomTrack(*picked))
						{
							return true;
						}
					}
					if (playlistSize <= 0)
					{
						return false;
					}
					r = customCount + getType("via.MathEx").call<std::int32_t>("randomRangeIndex", 0, playlistSize);
				}

				Object musicInfo = playlist[r - customCount];
				std::uint16_t musicId = musicInfo.get<std::uint16_t>("musicId");
				bool isArrange = musicInfo.get<bool>("isArrange");

				if (!MusicSystemMod::playBgm(srcObj, musicId, isArrange))
				{
					return false;
				}

				launcherReplacedBgmId = musicId;
				return true;
			};

			if (srcObj != soundLauncherBgmManager["GameObject"])
			{
				// ignore replace for sub player
				return REFRAMEWORK_HOOK_CALL_ORIGINAL;
			}

			launcherReplacedBgmId = soundMilkyDefine.get<std::uint16_t>("INVALID_BGM_ID");

			if (bgmId != soundMilkyDefine.get<std::uint16_t>("BGM_LAUNCHER_01") &&
				bgmId != soundMilkyDefine.get<std::uint16_t>("BGM_LAUNCHER_02") &&
				bgmId != soundMilkyDefine.get<std::uint16_t>("BGM_LAUNCHER_03") &&
				bgmId != soundMilkyDefine.get<std::uint16_t>("BGM_LAUNCHER_04"))
			{
				return REFRAMEWORK_HOOK_CALL_ORIGINAL;
			}

			bool replaced;
			CustomPlaylistMenuItem::Option optionValue = (CustomPlaylistMenuItem::Option)s_menuItemMainMenuBgm->getValue();
			switch (optionValue)
			{
			default:
				replaced = selectNoChange(optionValue);
				break;
			case CustomPlaylistMenuItem::Option::MusicOff:
				replaced = selectMusicOff(optionValue);
				break;
			case CustomPlaylistMenuItem::Option::Playlist:
			case CustomPlaylistMenuItem::Option::Favorites:
			case CustomPlaylistMenuItem::Option::CustomAndPlaylist:
				replaced = selectPlaylist(optionValue);
				break;
			case CustomPlaylistMenuItem::Option::CustomMusic:
				replaced = selectCustomMusic(optionValue);
				break;
			}

			if (!startedLauncherCustomAudio && s_isLauncherCustomAudio)
			{
				CustomAudioPlayer::instance().stop(LAUNCHER_CUSTOM_AUDIO_SLOT);
				s_isLauncherCustomAudio = false;
			}

			if (replaced)
			{
				return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
			}
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.sound.SoundLauncherBgmManager.stopBgm",
		[](int argc, void **argv, auto...)
		{
			Object soundLauncherBgmManager = Object(argv[1]);
			Object srcObj = Object(argv[3]);

			if (srcObj != soundLauncherBgmManager["GameObject"])
			{
				// ignore replace for sub player
				return REFRAMEWORK_HOOK_CALL_ORIGINAL;
			}

			if (!s_internalLauncherStop && s_isLauncherCustomAudio)
			{
				CustomAudioPlayer::instance().stop(LAUNCHER_CUSTOM_AUDIO_SLOT);
				s_isLauncherCustomAudio = false;
			}

			if (launcherReplacedBgmId == getType("app.sound.SoundMilkyDefine").get<std::uint16_t>("INVALID_BGM_ID"))
			{
				return REFRAMEWORK_HOOK_CALL_ORIGINAL;
			}

			// Stop the actual BGM that we're playing
			argv[2] = (void *)(intptr_t)launcherReplacedBgmId;
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	for (const char *stopMethod : {
			 "app.sound.SoundLauncherBgmManager.stopMainBgm",
			 "app.sound.SoundLauncherBgmManager.stopMainBgm2",
			 "app.sound.SoundLauncherBgmManager.stopAllBgm",
			 "app.sound.SoundLauncherBgmManager.onDestroy",
		 })
	{
		s_hooks.emplace_back(hook(
			stopMethod,
			[](auto...)
			{
				if (!s_internalLauncherStop && s_isLauncherCustomAudio)
				{
					CustomAudioPlayer::instance().stop(LAUNCHER_CUSTOM_AUDIO_SLOT);
					s_isLauncherCustomAudio = false;
				}
				return REFRAMEWORK_HOOK_CALL_ORIGINAL;
			},
			nullptr));
	}

	s_hooks.emplace_back(hook(
		"app.sound.SoundLauncherBgmManager.pauseBgm",
		[](auto...)
		{
			if (s_isLauncherCustomAudio)
			{
				CustomAudioPlayer::instance().pause(LAUNCHER_CUSTOM_AUDIO_SLOT);
			}
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.sound.SoundLauncherBgmManager.resumeBgm",
		[](auto...)
		{
			if (s_isLauncherCustomAudio)
			{
				CustomAudioPlayer::instance().resume(LAUNCHER_CUSTOM_AUDIO_SLOT);
			}
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.Launcher.setStandbyFlagLauncherOnlyGUIPrefabs",
		[](int argc, void **argv, auto...)
		{
			// Always keep Launcher GUI prefabs loaded
			// Allows loading Music Player while in-game
			argv[2] = (void *)(intptr_t)true;

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	static bool isGamePause = false;
	s_hooks.emplace_back(hook(
		"app.GUILauncherMusicPlayer.update",
		[](auto...)
		{
			// set app.Launcher._isGamePause to false during app.GUILauncherMusicPlayer.update
			// Allows Music Player to work in-game
			Object launcher = getSingleton("app.Launcher");
			isGamePause = launcher.get<bool>("_isGamePause");
			launcher.set<bool>("_isGamePause", false);

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		[](auto...)
		{
			// restore original value of app.Launcher._isGamePause
			Object launcher = getSingleton("app.Launcher");
			launcher.set<bool>("_isGamePause", isGamePause);
		}));

	static std::optional<bool> s_playMusicPlayerBgmReturnValue;
	s_hooks.emplace_back(hook(
		"app.sound.SoundMilkyManager.playMusicPlayerBgm",
		[](int argc, void **argv, auto...)
		{
			Object soundMilkyManager = argv[1];
			std::int32_t bgmId = (std::int32_t)(intptr_t)argv[2];

			switch ((app::AppDefine::GameType)getSingleton("app.Launcher").get<std::int32_t>("gameType"))
			{
			case app::AppDefine::GameType::rr1:
			case app::AppDefine::GameType::rr2:
			case app::AppDefine::GameType::rr3:
				std::optional<BgmTriggerInfo> bgmTriggerInfo = getBgmTriggerInfo(bgmId);
				if (!bgmTriggerInfo.has_value())
				{
					return REFRAMEWORK_HOOK_CALL_ORIGINAL;
				}
				Object container = bgmTriggerInfo.value().Container;
				std::uint32_t triggerId = bgmTriggerInfo.value().PlayTriggerID;

				soundMilkyManager["_CurrentPlayMpContainer"] = container;
				s_playMusicPlayerBgmReturnValue = soundMilkyManager.call<bool>(
					"playMusicPlayerBgmByTriggerId", triggerId);
				return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		[](void **retval, auto...)
		{
			if (s_playMusicPlayerBgmReturnValue.has_value())
			{
				*retval = (void *)(intptr_t)s_playMusicPlayerBgmReturnValue.value();
				s_playMusicPlayerBgmReturnValue.reset();
			}
		}));

	s_hooks.emplace_back(hook(
		"app.sound.SoundMilkyManager.stopMusicPlayerBgm",
		[](int argc, void **argv, auto...)
		{
			Object soundMilkyManager = argv[1];

			switch ((app::AppDefine::GameType)getSingleton("app.Launcher").get<std::int32_t>("gameType"))
			{
			case app::AppDefine::GameType::rr1:
			case app::AppDefine::GameType::rr2:
			case app::AppDefine::GameType::rr3:
				// Which BGM we are playing doesn't matter
				std::optional<BgmTriggerInfo> bgmTriggerInfo = getBgmTriggerInfo(0);
				if (!bgmTriggerInfo.has_value())
				{
					return REFRAMEWORK_HOOK_CALL_ORIGINAL;
				}
				Object container = bgmTriggerInfo.value().Container;
				std::uint32_t triggerId = bgmTriggerInfo.value().StopTriggerID;
				Object playerObject = soundMilkyManager["GameObject"];

				container.call(
					"trigger(System.UInt32, via.GameObject, via.GameObject, System.UInt32, System.Boolean, System.UInt32, via.simplewwise.CallbackType, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>)",
					triggerId,
					playerObject,
					nullptr,
					0xFFFFFFFF,
					false,
					0,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr);
				return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.sound.SoundMilkyManager.fadeOutStopMusicPlayerBgm",
		[](int argc, void **argv, auto...)
		{
			Object soundMilkyManager = argv[1];

			switch ((app::AppDefine::GameType)getSingleton("app.Launcher").get<std::int32_t>("gameType"))
			{
			case app::AppDefine::GameType::rr1:
			case app::AppDefine::GameType::rr2:
			case app::AppDefine::GameType::rr3:
				// Which BGM we are playing doesn't matter
				std::optional<BgmTriggerInfo> bgmTriggerInfo = getBgmTriggerInfo(0);
				if (!bgmTriggerInfo.has_value())
				{
					return REFRAMEWORK_HOOK_CALL_ORIGINAL;
				}
				Object container = bgmTriggerInfo.value().Container;
				std::uint32_t triggerId = bgmTriggerInfo.value().FadeOutTriggerID;
				Object playerObject = soundMilkyManager["GameObject"];

				container.call(
					"trigger(System.UInt32, via.GameObject, via.GameObject, System.UInt32, System.Boolean, System.UInt32, via.simplewwise.CallbackType, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>)",
					triggerId,
					playerObject,
					nullptr,
					0xFFFFFFFF,
					false,
					0,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr);
				return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.sound.SoundMilkyManager.pauseMusicPlayerBgm",
		[](int argc, void **argv, auto...)
		{
			Object soundMilkyManager = argv[1];

			switch ((app::AppDefine::GameType)getSingleton("app.Launcher").get<std::int32_t>("gameType"))
			{
			case app::AppDefine::GameType::rr1:
			case app::AppDefine::GameType::rr2:
			case app::AppDefine::GameType::rr3:
				// Which BGM we are playing doesn't matter
				std::optional<BgmTriggerInfo> bgmTriggerInfo = getBgmTriggerInfo(0);
				if (!bgmTriggerInfo.has_value())
				{
					return REFRAMEWORK_HOOK_CALL_ORIGINAL;
				}
				Object container = bgmTriggerInfo.value().Container;
				std::uint32_t triggerId = bgmTriggerInfo.value().PauseTriggerID;
				Object playerObject = soundMilkyManager["GameObject"];

				container.call(
					"trigger(System.UInt32, via.GameObject, via.GameObject, System.UInt32, System.Boolean, System.UInt32, via.simplewwise.CallbackType, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>)",
					triggerId,
					playerObject,
					nullptr,
					0xFFFFFFFF,
					false,
					0,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr);
				return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.sound.SoundMilkyManager.resumeMusicPlayerBgm",
		[](int argc, void **argv, auto...)
		{
			Object soundMilkyManager = argv[1];

			switch ((app::AppDefine::GameType)getSingleton("app.Launcher").get<std::int32_t>("gameType"))
			{
			case app::AppDefine::GameType::rr1:
			case app::AppDefine::GameType::rr2:
			case app::AppDefine::GameType::rr3:
				// Which BGM we are playing doesn't matter
				std::optional<BgmTriggerInfo> bgmTriggerInfo = getBgmTriggerInfo(0);
				if (!bgmTriggerInfo.has_value())
				{
					return REFRAMEWORK_HOOK_CALL_ORIGINAL;
				}
				Object container = bgmTriggerInfo.value().Container;
				std::uint32_t triggerId = bgmTriggerInfo.value().ResumeTriggerID;
				Object playerObject = soundMilkyManager["GameObject"];

				container.call(
					"trigger(System.UInt32, via.GameObject, via.GameObject, System.UInt32, System.Boolean, System.UInt32, via.simplewwise.CallbackType, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>, System.Action`1<soundlib.SoundManager.RequestInfo>)",
					triggerId,
					playerObject,
					nullptr,
					0xFFFFFFFF,
					false,
					0,
					0,
					nullptr,
					nullptr,
					nullptr,
					nullptr);
				return REFRAMEWORK_HOOK_SKIP_ORIGINAL;
			}

			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.sound.SoundMilkyManager.update",
		[](auto...)
		{
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		[](auto...)
		{
			MusicSystemMod::syncCustomAudioVolume();
		}));

	s_hooks.emplace_back(hook(
		"app.sound.SoundMilkyManager.setBgmMute",
		[](int argc, void **argv, auto...)
		{
			std::int32_t muteType = (std::int32_t)(intptr_t)argv[2];
			s_isBgmMutedByEvent = (muteType != 0);
			MusicSystemMod::syncCustomAudioVolume();
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		nullptr));

	s_hooks.emplace_back(hook(
		"app.sound.SoundMilkyManager.stopAllSound",
		[](auto...)
		{
			return REFRAMEWORK_HOOK_CALL_ORIGINAL;
		},
		[](auto...)
		{
			MusicSystemMod::stopAllIngameCustomAudio();
			if (s_isLauncherCustomAudio)
			{
				CustomAudioPlayer::instance().stop(LAUNCHER_CUSTOM_AUDIO_SLOT);
				s_isLauncherCustomAudio = false;
			}
		}));
}

/// @brief Uninstall all hooks used for music system mod
void MusicSystemMod::uninstallHooks()
{
	for (HookRef &hook : s_hooks)
	{
		hook.unhook();
	}
	s_hooks.clear();
}
