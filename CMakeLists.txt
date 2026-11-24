cmake_minimum_required(VERSION 3.21)
project(CODRedemptionHunter VERSION 6.4.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(HUNTER_BUILD_APP "Build the Qt 6 desktop app" ON)
option(HUNTER_WITH_WEBENGINE "Open redemption pages inside the app (needs Qt WebEngine)" ON)
option(HUNTER_BUILD_TESTS "Build the core tests" ON)

if(MSVC)
  add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus /EHsc)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

# ── SQLite: the system one if present, otherwise the official amalgamation ─────
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
  include(FetchContent)
  FetchContent_Declare(sqlite_amalgamation
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(sqlite_amalgamation)
  add_library(sqlite3_bundled STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
  target_include_directories(sqlite3_bundled PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
  target_compile_definitions(sqlite3_bundled PRIVATE SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION)
  add_library(SQLite::SQLite3 ALIAS sqlite3_bundled)
endif()

# ── core: scanner engine, plain C++20 (no Qt) ─────────────────────────────────
file(GLOB HUNTER_CORE_SOURCES CONFIGURE_DEPENDS core/*.cpp)
add_library(hunter_core STATIC ${HUNTER_CORE_SOURCES})
target_include_directories(hunter_core PUBLIC core)
target_link_libraries(hunter_core PUBLIC SQLite::SQLite3)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(hunter_core PRIVATE -Wall -Wextra)
endif()

if(HUNTER_BUILD_TESTS)
  enable_testing()
  add_executable(core_tests tests/core_tests.cpp)
  target_link_libraries(core_tests PRIVATE hunter_core)
  target_compile_definitions(core_tests PRIVATE HUNTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME core_tests COMMAND core_tests)
endif()

# ── desktop app ───────────────────────────────────────────────────────────────
if(HUNTER_BUILD_APP)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
  if(HUNTER_WITH_WEBENGINE)
    find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
    if(NOT Qt6WebEngineWidgets_FOUND)
      message(WARNING "Qt WebEngine not found: redemption pages will open in your browser instead.")
      set(HUNTER_WITH_WEBENGINE OFF)
    endif()
  endif()
  qt_standard_project_setup()

  file(GLOB HUNTER_APP_SOURCES CONFIGURE_DEPENDS app/*.cpp app/*.hpp)
  set(HUNTER_APP_EXTRA app/resources.qrc)
  if(WIN32)
    list(APPEND HUNTER_APP_EXTRA app/app.rc)
  endif()
  qt_add_executable(CODRedemptionHunter WIN32 MACOSX_BUNDLE ${HUNTER_APP_SOURCES} ${HUNTER_APP_EXTRA})
  set_target_properties(CODRedemptionHunter PROPERTIES AUTORCC ON)
  target_link_libraries(CODRedemptionHunter PRIVATE hunter_core Qt6::Widgets Qt6::Network)
  target_compile_definitions(CODRedemptionHunter PRIVATE
    HUNTER_VERSION="${PROJECT_VERSION}" HUNTER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  if(HUNTER_WITH_WEBENGINE)
    target_link_libraries(CODRedemptionHunter PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(CODRedemptionHunter PRIVATE HUNTER_WITH_WEBENGINE)
  endif()

  set(HUNTER_DATA_FILES data/config.json data/.env.example data/meta.json data/research_sources.json)
  file(GLOB HUNTER_I18N_FILES CONFIGURE_DEPENDS data/i18n/*.json)

  if(ANDROID)
    if(COMMAND qt_policy)
      qt_policy(SET QTP0002 NEW)
    endif()
    # Android: data files go inside the APK (:/shipped/...) and are copied out on first start.
    qt_add_resources(CODRedemptionHunter "shipped" PREFIX "/shipped" BASE data FILES ${HUNTER_DATA_FILES} ${HUNTER_I18N_FILES})
    set_target_properties(CODRedemptionHunter PROPERTIES
      QT_ANDROID_PACKAGE_NAME "io.github.codredemptionhunter"
      QT_ANDROID_APP_NAME "COD Redemption Hunter"
      QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
      QT_ANDROID_VERSION_CODE "${PROJECT_VERSION_MAJOR}${PROJECT_VERSION_MINOR}${PROJECT_VERSION_PATCH}"
      QT_ANDROID_MIN_SDK_VERSION 28
      QT_ANDROID_TARGET_SDK_VERSION 34
      QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
    # HTTPS on Android needs OpenSSL next to Qt — KDAB's prebuilt, maintained set.
    include(FetchContent)
    FetchContent_Declare(android_openssl
      URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(android_openssl)
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
    add_android_openssl_libraries(CODRedemptionHunter)
  else()
    # shipped defaults next to the exe
    add_custom_command(TARGET CODRedemptionHunter POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:CODRedemptionHunter>/data/i18n
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/data/config.json
              ${CMAKE_CURRENT_SOURCE_DIR}/data/.env.example ${CMAKE_CURRENT_SOURCE_DIR}/data/meta.json
              ${CMAKE_CURRENT_SOURCE_DIR}/data/research_sources.json $<TARGET_FILE_DIR:CODRedemptionHunter>/data/
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HUNTER_I18N_FILES} $<TARGET_FILE_DIR:CODRedemptionHunter>/data/i18n/)
  endif()

  install(TARGETS CODRedemptionHunter RUNTIME DESTINATION . BUNDLE DESTINATION . LIBRARY DESTINATION .)
  install(FILES ${HUNTER_DATA_FILES} DESTINATION data)
  install(FILES ${HUNTER_I18N_FILES} DESTINATION data/i18n)
  install(FILES README.md LICENSE DESTINATION .)
endif()
