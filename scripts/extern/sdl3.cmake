##### SDL3: client window and input (KISAK_CLIENT_SDL3) #####
if (NOT WIN32)
	# dxvk-native's SDL3 WSI drives the client's SDL_Window, so both must use
	# the one shared system SDL3; a second, static copy wouldn't share window
	# state (and the fetched build needs the X11 development packages).
	find_package(SDL3 3.4 CONFIG REQUIRED)
	set(KISAK_SDL3_TARGET SDL3::SDL3)
else()
include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare (
	SDL3
	# 3.4.18; the release tarball is pinned by hash because a URL can change.
	URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
	URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
)
FetchContent_MakeAvailable ( SDL3 )

if (MSVC)
	set_property(TARGET SDL3-static PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(KISAK_SDL3_TARGET SDL3::SDL3-static)
endif()

target_sources(${PROJECT_NAME} PRIVATE
	${SRC_DIR}/client/cl_sdl3.cpp
	${SRC_DIR}/client/cl_sdl3.h
	${SRC_DIR}/client/cl_sdl3_input.cpp
	${SRC_DIR}/client/cl_sdl3_keys.cpp
	${SRC_DIR}/client/cl_sdl3_keys.h
)
# cl_sdl3_input.cpp provides the IN_* layer and cl_sdl3.cpp the window
# procedure's state instead.
set_source_files_properties(
	${SRC_DIR}/win32/win_input.cpp
	${SRC_DIR}/win32/win_wndproc.cpp
	PROPERTIES HEADER_FILE_ONLY ON)
target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_CLIENT_SDL3)
target_link_libraries(${PROJECT_NAME} PRIVATE ${KISAK_SDL3_TARGET})
