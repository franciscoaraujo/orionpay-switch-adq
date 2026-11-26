cmake_minimum_required(VERSION 3.28)

# =========================================================
# Toolchain
# =========================================================

set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_settings.cmake
)

project(orionpay_switch_adq)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =========================================================
# Dependências
# =========================================================

include(FetchContent)

# Protobuf / gRPC
find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
message(STATUS "Protobuf version: ${Protobuf_VERSION}")

# Kafka
find_package(PkgConfig REQUIRED)
pkg_check_modules(RDKAFKA REQUIRED rdkafka++)
message(STATUS "RDKAFKA_INCLUDE_DIRS: ${RDKAFKA_INCLUDE_DIRS}")
message(STATUS "RDKAFKA_LIBRARIES: ${RDKAFKA_LIBRARIES}")

# OpenSSL
find_package(OpenSSL REQUIRED)

# PostgreSQL
find_package(PostgreSQL REQUIRED)

# =========================================================
# FetchContent
# =========================================================

# spdlog
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.9.2
)
FetchContent_MakeAvailable(spdlog)

# Asio
FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

# nlohmann_json
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.10.5
)
FetchContent_MakeAvailable(nlohmann_json)

# libpqxx
set(SKIP_BUILD_TEST OFF CACHE BOOL "Disable libpqxx tests" FORCE)
FetchContent_Declare(
        libpqxx
        GIT_REPOSITORY https://github.com/jtv/libpqxx.git
        GIT_TAG 7.9.1
)
FetchContent_MakeAvailable(libpqxx)

# =========================================================
# gRPC / Protobuf Generation
# =========================================================

set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/grpc)
set(PROTO_FILE ${PROTO_SRC_DIR}/acquirer.proto)
set(BASE_PROTO_NAME acquirer)
set(GENERATED_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_grpc_files)

set(PROTO_PB_H "${GENERATED_BUILD_DIR}/${BASE_PROTO_NAME}.pb.h")
set(PROTO_PB_CC "${GENERATED_BUILD_DIR}/${BASE_PROTO_NAME}.pb.cc")
set(GRPC_PB_H "${GENERATED_BUILD_DIR}/${BASE_PROTO_NAME}.grpc.pb.h")
set(GRPC_PB_CC "${GENERATED_BUILD_DIR}/${BASE_PROTO_NAME}.grpc.pb.cc")

add_custom_target(generate_grpc_files ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_BUILD_DIR}"
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        -I=${PROTO_SRC_DIR}
        --cpp_out=${GENERATED_BUILD_DIR}
        --grpc_out=${GENERATED_BUILD_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        BYPRODUCTS ${PROTO_PB_H} ${PROTO_PB_CC} ${GRPC_PB_H} ${GRPC_PB_CC}
        COMMENT "Generating gRPC files for ${PROTO_FILE}"
)

add_library(grpc_generated_lib STATIC
        ${PROTO_PB_CC}
        ${GRPC_PB_CC}
)
add_dependencies(grpc_generated_lib generate_grpc_files)

target_include_directories(grpc_generated_lib PUBLIC
        ${GENERATED_BUILD_DIR}
)

target_link_libraries(grpc_generated_lib PUBLIC
        gRPC::grpc++
        protobuf::libprotobuf
)

# =========================================================
# Main Executable
# =========================================================

add_executable(orionpay_switch_adq
        src/main.cpp
        src/grpc/grpc_server.cpp

        src/common/logger.cpp
        src/common/uuid_generator.cpp
        src/config/config_manager.cpp

        src/network/visanet_client.cpp

        src/iso8583/iso_parser.cpp
        src/iso8583/iso_builder.cpp
        src/iso8583/iso_message.cpp
        src/iso8583/bitmap.cpp
        # src/iso8583/field.cpp # Removed

        src/switch/router.cpp
        src/switch/transaction_manager.cpp
        src/switch/circuit_breaker.cpp

        src/issuer/issuer_service.cpp
        src/metrics/metrics_registry.cpp
        src/persistence/transaction_repository.cpp
        src/database/database_manager.cpp
        src/database/pg_worker.cpp
        src/crypto/hsm_worker.cpp
        src/saf/saf_manager.cpp
)

add_dependencies(orionpay_switch_adq generate_grpc_files)

# =========================================================
# Includes
# =========================================================

target_include_directories(orionpay_switch_adq PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        SYSTEM ${GENERATED_BUILD_DIR}
        ${asio_SOURCE_DIR}/asio/include
        ${nlohmann_json_SOURCE_DIR}/include
        ${PostgreSQL_INCLUDE_DIRS}
        ${libpqxx_SOURCE_DIR}/include
        ${spdlog_SOURCE_DIR}/include
        ${RDKAFKA_INCLUDE_DIRS}
)

# =========================================================
# Linking
# =========================================================

target_link_libraries(orionpay_switch_adq PRIVATE
        grpc_generated_lib
        gRPC::grpc++
        protobuf::libprotobuf
        ${RDKAFKA_LIBRARIES}
        spdlog::spdlog
        pqxx
        ${PostgreSQL_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
)
