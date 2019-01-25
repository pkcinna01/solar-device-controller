

cmake_minimum_required(VERSION 3.9)
project(solar_ifttt)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
#set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
#find_library(SSL_LIB libssl.a REQUIRED)
#find_library(CRYPTO_LIB libcrypto.a REQUIRED)

#message(${SSL_LIB})
#message(${CRYPTO_LIB})

add_executable(solar_ifttt main.cpp 
        automation/sensor/Sensor.cpp 
        automation/device/Device.cpp
        automation/capability/Capability.cpp
        automation/constraint/Constraint.cpp
        ifttt/ifttt.cpp
        Prometheus.h
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

target_include_directories(solar_ifttt PRIVATE "${POCO_INCLUDE_DIR}")
target_link_libraries(solar_ifttt "${POCO_LIBS}" "${SSL_LIB}" "${CRYPTO_LIB}" -ldl  Threads::Threads)
