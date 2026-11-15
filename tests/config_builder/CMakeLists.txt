# Builds the real config generator of the GUI (db/ConfigBuilder.cpp and the server formats) into a small
# command line program. It writes the sing-box config for a set of typical settings, so the output can be
# checked by the core (`nekobox_core check`) and compared between versions.
cmake_minimum_required(VERSION 3.16)
project(config_builder_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(config_builder_test
        main.cpp
        ${ROOT}/db/ConfigBuilder.cpp
        ${ROOT}/db/Database.cpp
        ${ROOT}/main/NekoGui.cpp
        ${ROOT}/main/NekoGui_Utils.cpp
        ${ROOT}/main/HTTPRequestHelper.cpp
        ${ROOT}/fmt/AbstractBean.cpp
        ${ROOT}/fmt/Bean2CoreObj_box.cpp
        ${ROOT}/fmt/Bean2External.cpp
        ${ROOT}/fmt/Bean2Link.cpp
        ${ROOT}/fmt/Link2Bean.cpp
        ${ROOT}/sys/ExternalProcess.cpp
        ${ROOT}/3rdparty/base64.cpp
        ${ROOT}/sys/ExternalProcess.hpp
        ${ROOT}/main/HTTPRequestHelper.hpp
)
if (UNIX)
    target_sources(config_builder_test PRIVATE ${ROOT}/sys/linux/LinuxCap.cpp)
endif ()
if (WIN32)
    target_sources(config_builder_test PRIVATE ${ROOT}/sys/windows/guihelper.cpp)
    target_link_libraries(config_builder_test PRIVATE advapi32 user32 shell32 ole32)
endif ()
target_include_directories(config_builder_test PRIVATE ${ROOT} ${ROOT}/3rdparty)
target_compile_definitions(config_builder_test PRIVATE NKR_NO_GRPC NKR_NO_YAML NKR_NO_ZXING NKR_NO_QHOTKEY NKR_VERSION="test" NKR_CONFIG_BUILDER_TEST NKR_SOURCE_DIR="${ROOT}")
target_link_libraries(config_builder_test PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
