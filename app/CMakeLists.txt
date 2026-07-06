find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent)
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)

qt_standard_project_setup()

qt_add_executable(GameCQ WIN32
    main.cpp
    core/AppPaths.cpp
    core/AppPaths.h
    launcher/LaunchCatalog.cpp
    launcher/LaunchCatalog.h
    launcher/LaunchEntryDialog.cpp
    launcher/LaunchEntryDialog.h
    launcher/LegacyUpdateRunner.cpp
    launcher/LegacyUpdateRunner.h
    launcher/LaunchMetadata.cpp
    launcher/LaunchMetadata.h
    launcher/LaunchRunner.cpp
    launcher/LaunchRunner.h
    launcher/HttpManifestUpdater.cpp
    launcher/HttpManifestUpdater.h
    net/HttpFetch.cpp
    net/HttpFetch.h
    net/MetaClientBridge.cpp
    net/MetaClientBridge.h
    net/GameProtocolConstants.h
    net/MetaClientTypes.h
    net/ProfileFlags.h
    net/MetaClientWorker.cpp
    net/MetaClientWorker.h
    security/CredentialStore.cpp
    security/CredentialStore.h
    ui/EmoteStore.cpp
    ui/HelpDialogs.cpp
    ui/EmoteStore.h
    ui/HelpDialogs.h
    ui/BrowserSupport.cpp
    ui/BrowserPage.cpp
     ui/BrowserSupport.h
     ui/BrowserPage.h
     ui/ChatFormatting.cpp
     ui/ChatFormattingMarkup.cpp
     ui/ChatHtmlRenderer.cpp
    ui/ChatLogWriter.cpp
    ui/ChatMediaCache.cpp
    ui/ChatFormatting.h
    ui/ChatHtmlRenderer.h
    ui/ChatLogWriter.h
    ui/ChatMediaCache.h
    ui/ChatUiSupport.h
    ui/DiscordChatMedia.cpp
    ui/DiscordChatMedia.h
    ui/LegacyIcons.cpp
    ui/LegacyIcons.h
    ui/LaunchWidgets.cpp
    ui/LaunchWidgets.h
    ui/LaunchPresentation.cpp
    ui/LaunchPresentation.h
    ui/LaunchInfoList.cpp
    ui/LaunchInfoList.h
    ui/LoginDialog.cpp
    ui/LoginDialog.h
    ui/ProfilePresentation.cpp
    ui/ProfilePresentation.h
    ui/SideDrawerItems.cpp
    ui/SideDrawerItems.h
    ui/MainWindow.cpp
    ui/MainWindowBridgeHandlers.cpp
    ui/MainWindowBrowser.cpp
    ui/MainWindowChat.cpp
    ui/MainWindowChatComposer.cpp
    ui/MainWindowChatLog.cpp
    ui/MainWindowLaunchActions.cpp
    ui/MainWindowLaunchDetails.cpp
    ui/MainWindowLaunchEdit.cpp
    ui/MainWindowLaunchLibrary.cpp
    ui/MainWindowLaunchNews.cpp
    ui/MainWindowLaunchPage.cpp
    ui/MainWindowLaunchRun.cpp
    ui/MainWindowLaunchServers.cpp
    ui/MainWindowLaunchUpdates.cpp
     ui/MainWindowLaunchSupport.h
     ui/MainWindowProfileAccess.cpp
     ui/MainWindowSideDrawer.cpp
     ui/MainWindowSideDrawerUsers.cpp
     ui/MainWindowShell.cpp
     ui/MainWindowSession.cpp
     ui/MainWindowToolbar.cpp
     ui/MainWindowTray.cpp
     ui/MainWindow.h
     ui/SettingsDialog.cpp
     ui/SettingsDialogEmotes.cpp
     ui/SettingsDialogPages.cpp
     ui/SettingsDialogTheme.cpp
    ui/SettingsDialog.h
    ui/SettingsDialogThemeSupport.h
    ui/StatusLine.cpp
    ui/StatusLine.h
     ui/ThemeManager.cpp
     ui/ThemePresets.cpp
     ui/ThemeSettings.cpp
     ui/ThemeStyleClassic.cpp
     ui/ThemeStyleConsole.cpp
     ui/ThemeStyleEmerald.cpp
     ui/ThemeStyleOak.cpp
     ui/ThemeStylePolish.cpp
     ui/ThemeStyleVapor.cpp
     ui/ThemeStyles.cpp
    ui/UserDialogs.cpp
     ui/ThemeManager.h
     ui/ThemePresets.h
     ui/ThemeSettings.h
     ui/ThemeStylePolish.h
     ui/UserDialogs.h
    ui/WindowChrome.cpp
    ui/WindowChrome.h
)

if(WIN32)
    target_sources(GameCQ PRIVATE res/GameCQ.rc)
endif()

set_source_files_properties(res/theme.qss PROPERTIES
    QT_RESOURCE_ALIAS "theme.qss"
)
set_source_files_properties(res/theme_ember.qss PROPERTIES
    QT_RESOURCE_ALIAS "theme_ember.qss"
)

qt_add_resources(GameCQ "gamecq_theme_resources"
    PREFIX "/gamecq"
    FILES
        res/theme.qss
        res/theme_ember.qss
)

file(GLOB GAMECQ_LEGACY_ICON_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Icons/*.ico"
)

qt_add_resources(GameCQ "gamecq_legacy_icon_resources"
    PREFIX "/gamecq/icons"
    BASE "${CMAKE_SOURCE_DIR}/Icons"
    FILES
        ${GAMECQ_LEGACY_ICON_FILES}
)

target_link_libraries(GameCQ
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        GameCQ::libnet
)

if(WIN32)
    target_link_libraries(GameCQ PRIVATE Advapi32)

    if(GAMECQ_BUILD_LEGACY_D9_HELPER)
        add_custom_target(GameCQLegacyUpdateX86 ALL
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/build_legacy_updater_x86.ps1"
                -Configuration "Release"
                -OutputDir "$<TARGET_FILE_DIR:GameCQ>"
            DEPENDS
                "${CMAKE_SOURCE_DIR}/scripts/build_legacy_updater_x86.ps1"
                "${CMAKE_SOURCE_DIR}/legacy_updater/CMakeLists.txt"
                "${CMAKE_SOURCE_DIR}/legacy_updater/LegacyClientUpdateTool.cpp"
                "${CMAKE_SOURCE_DIR}/libnet/CMakeLists.txt"
            VERBATIM
        )
        add_dependencies(GameCQ GameCQLegacyUpdateX86)
    endif()
endif()

if(TARGET Qt6::WebEngineWidgets)
    target_link_libraries(GameCQ PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(GameCQ PRIVATE GAMECQ_HAS_WEBENGINE=1)
endif()

target_include_directories(GameCQ PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(GameCQ PROPERTIES
    OUTPUT_NAME "GameCQ"
)






