cmake_minimum_required(
	VERSION 3.20.0
)

project(
	ExpandedMusicOptions
	VERSION 1.0.0
	LANGUAGES C CXX
)
set(GAME_INSTALL_EXE "MEGAMAN_STARFORCE_Legacy_Collection.exe" CACHE STRING "Game executable")
set(GAME_INSTALL_DIR_NAME "MegaMan StarForce LegacyCollection" CACHE STRING "Game directory name")

cmake_path(SET STEAM_LIBRARY_DIR "C:\\Program Files (x86)\\Steam\\")
cmake_path(SET GAME_INSTALL_DIR "${STEAM_LIBRARY_DIR}/steamapps/common/${GAME_INSTALL_DIR_NAME}/")

set(STEAM_LIBRARY_DIR "${STEAM_LIBRARY_DIR}" CACHE PATH "Steam library directory")
set(GAME_INSTALL_DIR "${GAME_INSTALL_DIR}" CACHE PATH "Game installation directory")

if(MSVC)
	add_compile_options(/utf-8)
endif()

# Game_Music_Emu (libgme) static library for .nsf, .nsfe, .spc, .vgm, .vgz, .gbs, .hes, .ay, .kss, .sap, .gym
file(GLOB GME_SOURCES
	"modules/game-music-emu/gme/*.cpp"
	"modules/game-music-emu/gme/ext/emu2413.c"
)
list(FILTER GME_SOURCES EXCLUDE REGEX "Ym2612_(MAME|Nuked)\\.cpp$")

add_library(gme_static STATIC ${GME_SOURCES})
target_include_directories(gme_static PUBLIC
	"modules/game-music-emu"
	"modules/game-music-emu/gme"
)
target_compile_definitions(gme_static PRIVATE
	BLARGG_LITTLE_ENDIAN=1
	VGM_YM2612_GENS=1
	_CRT_SECURE_NO_WARNINGS
)

include_directories(SYSTEM
	"generated"
	"src"
	"modules/REFramework/include"
	"modules/REFramework/dependencies"
	"modules/miniaudio"
	"modules/game-music-emu"
)
add_library(REF_PLUGIN SHARED
	"src/AudioMenuExtension.cpp"
	"src/CustomAudioPlayer.cpp"
	"src/CustomMusicLibrary.cpp"
	"src/CustomPlaylistMenuItem.cpp"
	"src/Guid.cpp"
	"src/Main.cpp"
	"src/MenuItem.cpp"
	"src/MessageFile.cpp"
	"src/MessageManager.cpp"
	"src/MessageReference.cpp"
	"src/MessageUtility.cpp"
	"src/MusicSystemMod.cpp"
	"src/MusicSystemMod_Hooks.cpp"
	"src/MusicSystemMod_Menu.cpp"
	"src/MusicSystemMod_Settings.cpp"
	"src/MusicSystemMod_Strings.cpp"
	"src/REFrameworkHelper.cpp"
	"src/ReplaceMusicMenuItem.cpp"
)

target_link_libraries(REF_PLUGIN PRIVATE
	gme_static
	ole32
	shell32
	uuid
)

add_definitions(
	-DUNICODE
	-D_CRT_SECURE_NO_WARNINGS
)
target_compile_features(REF_PLUGIN PUBLIC
	cxx_std_23
)

set_target_properties(REF_PLUGIN PROPERTIES
	OUTPUT_NAME ${PROJECT_NAME}
)

# Sanitize extracted Enums_Internal.hpp (change enum to enum class, deduplicate enum names)
find_package(Python 3.8 REQUIRED)
if(EXISTS "${GAME_INSTALL_DIR}/Enums_Internal.hpp")
	execute_process(
		COMMAND ${Python_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sanitize_enums.py" "${GAME_INSTALL_DIR}/Enums_Internal.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/generated/Enums_Internal.hpp"
	)
else()
	message(WARNING "Enums_Internal.hpp not found in ${GAME_INSTALL_DIR}. Launch the game once and use REFramework -> DeveloperTools -> ObjectExplorer -> Dump SDK.")
endif()
