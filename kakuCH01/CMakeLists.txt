cmake_minimum_required(VERSION 3.16)

project(kakuCH01 VERSION 1.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Network Quick Qml QuickControls2)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Network Quick Qml QuickControls2)
find_package(Qt6 REQUIRED COMPONENTS ShaderTools)

set(PROJECT_SOURCES
        main.cpp
        chapter.h
        novelwork.h
        glossary.h
        glossarymanager.h
        glossarymanager.cpp
        cjdlexporter.h
        cjdlexporter.cpp
        novelstore.h
        novelstore.cpp
        kakuyomuparser.h
        kakuyomuparser.cpp
        novelparser.h
        novelparser.cpp
        novelpaths.h
        novelpaths.cpp
        browserpagefetcher.h
        browserpagefetcher.cpp
        httpclient.h
        httpclient.cpp
        txtexporter.h
        txtexporter.cpp
        kakuyomuservice.h
        kakuyomuservice.cpp
        sakuratranslator.h
        sakuratranslator.cpp
        sakuramanager.h
        sakuramanager.cpp
        murasakitranslator.h
        murasakitranslator.cpp
        murasakimanager.h
        murasakimanager.cpp
        qwenmanager.h
        qwenmanager.cpp
        serverexecutable.h
        serverexecutable.cpp
        modelgpuconfig.h
        modelgpuconfig.cpp
        qwentranslator.h
        qwentranslator.cpp
        sitebrowser.h
        sitebrowser.cpp
        controllers/AppController.h
        controllers/AppController.cpp
        models/ChapterModel.h
        models/ChapterModel.cpp
        models/TranslationModel.h
        models/TranslationModel.cpp
        models/LibraryModel.h
        models/LibraryModel.cpp
        models/ReaderModel.h
        models/ReaderModel.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(kakuCH01
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(kakuCH01 SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(kakuCH01
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(kakuCH01 PRIVATE
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Qml
    Qt${QT_VERSION_MAJOR}::QuickControls2
)

target_compile_definitions(kakuCH01 PRIVATE APP_VERSION="${PROJECT_VERSION}")
qt_add_resources(kakuCH01 "application_icon"
    PREFIX "/"
    FILES icons/app.ico
)
if(WIN32)
    configure_file(app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/app.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/icons/app.ico")
    target_sources(kakuCH01 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

# 标记单例 QML 文件（必须在 qt_add_qml_module 之前设置）
set_source_files_properties(
    qml/Navigation.qml
    qml/theme/Colors.qml
    qml/theme/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

# QML 模块：统一注册 UI 资源
if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_qml_module(kakuCH01
        URI KakuCH
        VERSION 1.0
        QML_FILES
            qml/Main.qml
            qml/Navigation.qml
            qml/LocalPaths.js
            qml/components/Sidebar.qml
            qml/components/TopBar.qml
            qml/components/PrimaryButton.qml
            qml/components/SecondaryButton.qml
            qml/components/StatusBadge.qml
            qml/components/EmptyState.qml
            qml/components/GlassPanel.qml
            qml/components/GlassDialog.qml
            qml/components/GlassPopup.qml
            qml/components/GlassComboBox.qml
            qml/components/LiquidGlass.qml
            qml/components/NovelCover.qml
            qml/components/CapsuleTabBar.qml
            qml/components/GlossaryWindow.qml
            qml/components/ModelBar.qml
            qml/components/ModelGpuConfigDialog.qml
            qml/components/TranslationActions.qml
            qml/components/TranslationSelectionBar.qml
            qml/components/UserGuideDialog.qml
            qml/components/TranslationQueue.qml
            qml/components/SiteModeSwitcher.qml
            qml/pages/HomePage.qml
            qml/pages/DownloadPage.qml
            qml/pages/SitePage.qml
            qml/pages/TranslatePage.qml
            qml/pages/MurasakiTranslatePage.qml
            qml/pages/TranslateHubPage.qml
            qml/pages/QwenTranslatePage.qml
            qml/pages/ReaderPage.qml
            qml/pages/NovelDetailPage.qml
            qml/pages/SettingsPage.qml
            qml/theme/Colors.qml
            qml/theme/Theme.qml
    )
endif()

qt_add_resources(kakuCH01 "help_documents"
    PREFIX "/help"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/docs"
    FILES docs/USER_GUIDE.md
)

qt_add_shaders(kakuCH01 "liquidglass_shaders"
    PREFIX "/KakuCH"
    FILES shaders/liquidglass.frag
)

if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.kakuCH01)
endif()
set_target_properties(kakuCH01 PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS kakuCH01
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(kakuCH01)
endif()

