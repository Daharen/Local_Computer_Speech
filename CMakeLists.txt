cmake_minimum_required(VERSION 3.21)
project(LocalComputerSpeech VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(LocalComputerSpeech
    src/main.cpp
    src/app/Application.cpp
    src/app/Application.h
    src/core/PathResolver.cpp
    src/core/PathResolver.h
    src/core/BackendBridge.cpp
    src/core/BackendBridge.h
    src/core/SoxInstaller.cpp
    src/core/SoxInstaller.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/TrayController.cpp
    src/ui/TrayController.h
)

target_link_libraries(LocalComputerSpeech PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
)

target_include_directories(LocalComputerSpeech PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(LocalComputerSpeech PRIVATE
    QT_NO_KEYWORDS
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(LocalComputerSpeechSoxInstallerTests
        tests/TestSoxInstaller.cpp
        src/core/PathResolver.cpp
        src/core/PathResolver.h
        src/core/SoxInstaller.cpp
        src/core/SoxInstaller.h
    )
    target_include_directories(LocalComputerSpeechSoxInstallerTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(LocalComputerSpeechSoxInstallerTests PRIVATE
        QT_NO_KEYWORDS
    )
    target_link_libraries(LocalComputerSpeechSoxInstallerTests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )

    add_executable(LocalComputerSpeechBackendBridgeTests
        tests/TestBackendBridge.cpp
        src/core/BackendBridge.cpp
        src/core/BackendBridge.h
        src/core/PathResolver.cpp
        src/core/PathResolver.h
        src/core/SoxInstaller.cpp
        src/core/SoxInstaller.h
    )
    target_include_directories(LocalComputerSpeechBackendBridgeTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(LocalComputerSpeechBackendBridgeTests PRIVATE
        QT_NO_KEYWORDS
    )
    target_link_libraries(LocalComputerSpeechBackendBridgeTests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )

    add_test(NAME LocalComputerSpeechSoxInstallerTests COMMAND LocalComputerSpeechSoxInstallerTests)
    add_test(NAME LocalComputerSpeechBackendBridgeTests COMMAND LocalComputerSpeechBackendBridgeTests)
endif()
