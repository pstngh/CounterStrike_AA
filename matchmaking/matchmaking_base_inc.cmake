set(CMAKE_MODULE_PATH ${SRCDIR}/cmake)
include(${CMAKE_MODULE_PATH}/common_functions.cmake)

MacroRequired(SRCDIR)
MacroRequired(OUTLIBNAME)

include(${CMAKE_MODULE_PATH}/source_lib_base.cmake)

target_compile_definitions(${OUTLIBNAME} PRIVATE -DNO_STRING_T -DVECTOR -DVERSION_SAFE_STEAM_API_INTERFACES -DPROTECTED_THINGS_ENABLE -DNO_STEAM_GAMECOORDINATOR)
target_include_directories(${OUTLIBNAME} PRIVATE ${SRCDIR}/thirdparty/protobuf-2.5.0/src)

if( LINUXALL )
    target_compile_options(${OUTLIBNAME} PRIVATE -fpic -fno-semantic-interposition)
endif()

target_sources(${OUTLIBNAME} PRIVATE "matchmakingqos.cpp")
target_sources(${OUTLIBNAME} PRIVATE "mm_events.cpp")
target_sources(${OUTLIBNAME} PRIVATE "mm_extensions.cpp")
target_sources(${OUTLIBNAME} PRIVATE "mm_framework.cpp")
target_sources(${OUTLIBNAME} PRIVATE "mm_netmsgcontroller.cpp")
if( MM_DS MATCHES "0" )
    target_sources(${OUTLIBNAME} PRIVATE "mm_session.cpp")
endif()
target_sources(${OUTLIBNAME} PRIVATE "mm_voice.cpp")

if( MM_DS MATCHES "0" )
    target_sources(${OUTLIBNAME} PRIVATE "ds_searcher.cpp")
    target_sources(${OUTLIBNAME} PRIVATE "match_searcher.cpp")
endif()

target_sources(${OUTLIBNAME} PRIVATE "mm_netmgr.cpp")

if( MM_DS MATCHES "0" )
    target_sources(${OUTLIBNAME} PRIVATE "mm_session_offline_custom.cpp")
    target_sources(${OUTLIBNAME} PRIVATE "mm_session_online_client.cpp")
    target_sources(${OUTLIBNAME} PRIVATE "mm_session_online_host.cpp")
    target_sources(${OUTLIBNAME} PRIVATE "mm_session_online_search.cpp")
    target_sources(${OUTLIBNAME} PRIVATE "mm_session_online_teamsearch.cpp")
    target_sources(${OUTLIBNAME} PRIVATE "sys_session.cpp")
endif()

target_sources(${OUTLIBNAME} PRIVATE "steam_apihook.cpp")
target_sources(${OUTLIBNAME} PRIVATE "steam_apihook.h")
target_sources(${OUTLIBNAME} PRIVATE "steam_datacenterjobs.h")
target_sources(${OUTLIBNAME} PRIVATE "steam_datacenterjobs.cpp")
if( NOT DEDICATED )
    target_sources(${OUTLIBNAME} PRIVATE "steam_lobbyapi.cpp")
endif()
target_sources(${OUTLIBNAME} PRIVATE "steam_lobbyapi.h")

#TODO: can we remove this??
target_sources(${OUTLIBNAME} PRIVATE "x360_xlsp_cmd.cpp")

target_sources(${OUTLIBNAME} PRIVATE "datacenter.cpp")
target_sources(${OUTLIBNAME} PRIVATE "mm_dlc.cpp")
if( MM_DS MATCHES "0" )
    target_sources(${OUTLIBNAME} PRIVATE "leaderboards.cpp")
endif()
target_sources(${OUTLIBNAME} PRIVATE "matchsystem.cpp")
if( MM_DS MATCHES "0" )
    target_sources(${OUTLIBNAME} PRIVATE "player.cpp")
endif()
target_sources(${OUTLIBNAME} PRIVATE "playermanager.cpp")
if( MM_DS MATCHES "0" )
    target_sources(${OUTLIBNAME} PRIVATE "searchmanager.cpp")
endif()
target_sources(${OUTLIBNAME} PRIVATE "servermanager.cpp")
target_sources(${OUTLIBNAME} PRIVATE "playerrankingdata.cpp")

target_sources(${OUTLIBNAME} PRIVATE "extkeyvalues.cpp")

target_sources(${OUTLIBNAME} PRIVATE "${SRCDIR}/public/filesystem_helpers.cpp")
target_sources(${OUTLIBNAME} PRIVATE "main.cpp")

target_link_libraries(${OUTLIBNAME} kisak_gcsdk_client)
