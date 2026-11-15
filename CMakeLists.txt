cmake_minimum_required(VERSION 3.16)

project(deskpi VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Network WebSockets)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appdeskpi
    main.cpp
    BacklightIo.cpp BacklightIo.h
    BrightnessController.cpp BrightnessController.h
    media/RoundedArtwork.h
    BrightnessPolicy.h
)
target_compile_features(appdeskpi PRIVATE cxx_std_17)

qt_add_qml_module(appdeskpi
    URI deskpi
    VERSION 1.0
    QML_FILES
        Main.qml
        BrightnessSettings.qml
        BrightnessIcon.qml
        MediaScreen.qml
)

set_target_properties(appdeskpi PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

add_library(deskpi_admin STATIC
    SettingsManager.cpp SettingsManager.h AdminAuth.cpp AdminAuth.h
    AdminServer.cpp AdminServer.h SystemMetrics.cpp SystemMetrics.h
    media/MediaReceiver.cpp media/MediaReceiver.h media/Protocol.h)
target_include_directories(deskpi_admin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(deskpi_admin PRIVATE cxx_std_17)
target_link_libraries(deskpi_admin PUBLIC Qt6::Gui Qt6::WebSockets Qt6::Network OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

target_link_libraries(appdeskpi
    PRIVATE deskpi_admin Qt6::Quick Qt6::Network OpenSSL::SSL OpenSSL::Crypto Threads::Threads
)

qt_add_resources(deskpi_admin web_assets PREFIX "/" FILES
    web/index.html web/style.css web/app.js web/index.html.gz web/style.css.gz web/app.js.gz)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(brightness_tests tests/brightness_tests.cpp
        BacklightIo.cpp BacklightIo.h BrightnessController.cpp BrightnessController.h)
    target_include_directories(brightness_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(brightness_tests PRIVATE cxx_std_17)
    target_link_libraries(brightness_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME brightness COMMAND brightness_tests)
    qt_add_executable(admin_tests tests/admin_tests.cpp
        BacklightIo.cpp BacklightIo.h BrightnessController.cpp BrightnessController.h)
    target_include_directories(admin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(admin_tests PRIVATE cxx_std_17)
    target_link_libraries(admin_tests PRIVATE deskpi_admin Qt6::Test Qt6::Qml)
    add_test(NAME admin COMMAND admin_tests)
    qt_add_executable(brightness_ui_tests tests/brightness_ui_tests.cpp SettingsManager.cpp SettingsManager.h
        BacklightIo.cpp BacklightIo.h BrightnessController.cpp BrightnessController.h)
    target_include_directories(brightness_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(brightness_ui_tests PRIVATE cxx_std_17)
    target_compile_definitions(brightness_ui_tests PRIVATE DESKPI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(brightness_ui_tests PRIVATE Qt6::Quick Qt6::Test)
    add_test(NAME brightness_ui COMMAND brightness_ui_tests)
    set_tests_properties(brightness_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

include(GNUInstallDirs)
install(TARGETS appdeskpi
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

option(DESKBUDDY_BUILD_AGENT "Build the Linux companion" ON)
if(DESKBUDDY_BUILD_AGENT)
    find_package(Qt6 6.8 REQUIRED COMPONENTS DBus Concurrent)
    add_subdirectory(companion)
endif()
if(BUILD_TESTING AND DESKBUDDY_BUILD_AGENT)
    qt_add_executable(media_tests tests/media_tests.cpp BacklightIo.cpp BrightnessController.cpp)
    target_link_libraries(media_tests PRIVATE deskpi_admin deskbuddy_companion Qt6::Test)
    target_compile_definitions(media_tests PRIVATE DESKBUDDY_AGENT_PATH="$<TARGET_FILE:deskbuddy-agent>")
    add_dependencies(media_tests deskbuddy-agent)
    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    add_test(NAME media COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:media_tests>)
endif()
if(BUILD_TESTING AND DESKBUDDY_BUILD_AGENT)
    qt_add_executable(media_ui_tests tests/media_ui_tests.cpp media/RoundedArtwork.h BacklightIo.cpp BrightnessController.cpp)
    target_compile_definitions(media_ui_tests PRIVATE DESKPI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(media_ui_tests PRIVATE deskpi_admin deskbuddy_companion Qt6::Quick Qt6::Test)
    add_test(NAME media_ui COMMAND media_ui_tests)
    set_tests_properties(media_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
