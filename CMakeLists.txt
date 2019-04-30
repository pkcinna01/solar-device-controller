

cmake_minimum_required(VERSION 3.9)
project(solar-device-controller)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
#set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
#find_library(SSL_LIB libssl.a REQUIRED)
#find_library(CRYPTO_LIB libcrypto.a REQUIRED)

#message(${SSL_LIB})
#message(${CRYPTO_LIB})

add_executable(solar-device-controller main.cpp 
        AutomationPlatformSpecific.cpp
        DeviceControllerApp.cpp
        automation/sensor/Sensor.cpp 
        automation/device/Device.cpp
        automation/capability/Capability.cpp
        automation/constraint/Constraint.cpp
        HttpServer.cpp
        SolarMetrics.h
        automation/constraint/ScheduledConstraint.h
        automation/constraint/BooleanConstraint.h
        automation/constraint/NestedConstraint.h
        automation/constraint/ValueConstraint.h
        automation/sensor/CompositeSensor.h)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
#set(OPENSSL_USE_STATIC_LIBS TRUE)
#set(CRYPTO_USE_STATIC_LIBS TRUE)

set(POCO_PREFIX      "/usr/local")
set(POCO_INCLUDE_DIR "${POCO_PREFIX}/include")
set(POCO_LIB_DIR     "${POCO_PREFIX}/lib")
set(POCO_LIBS        "${POCO_LIB_DIR}/libPocoNetd.so"
        "${POCO_LIB_DIR}/libPocoNetSSLd.so"
        "${POCO_LIB_DIR}/libPocoUtild.so"
        "${POCO_LIB_DIR}/libPocoJSONd.so"
        "${POCO_LIB_DIR}/libPocoCryptod.so"
        "${POCO_LIB_DIR}/libPocoXMLd.so"
        "${POCO_LIB_DIR}/libPocoFoundationd.so")

set(PROMETHEUS_DEPLOY_DIR "/usr/local")
set(PROMETHEUS_INCLUDE_DIR "${PROMETHEUS_DEPLOY_DIR}/include")
set(PROMETHEUS_LIBS "${PROMETHEUS_DEPLOY_DIR}/lib/libprometheus-cpp-core.so"
        "${PROMETHEUS_DEPLOY_DIR}/lib/libprometheus-cpp-push.so"
        "${PROMETHEUS_DEPLOY_DIR}/lib/libprometheus-cpp-pull.so"
)

target_include_directories(solar-device-controller PRIVATE "${POCO_INCLUDE_DIR}" "${PROMETHEUS_INCLUDE_DIR}")
target_link_libraries(solar-device-controller "${POCO_LIBS}" "${PROMETHEUS_LIBS}" "${SSL_LIB}" "${CRYPTO_LIB}" -ldl  Threads::Threads)
