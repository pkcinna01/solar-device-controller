

cmake_minimum_required(VERSION 3.9)
project(solar-power-mgr)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
#set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
#find_library(SSL_LIB libssl.a REQUIRED)
#find_library(CRYPTO_LIB libcrypto.a REQUIRED)

#message(${SSL_LIB})
#message(${CRYPTO_LIB})

add_executable(solar-power-mgr main.cpp 
        AutomationPlatformSpecific.cpp
        SolarPowerMgrApp.cpp
        automation/sensor/Sensor.cpp 
        automation/device/Device.cpp
        automation/capability/Capability.cpp
        automation/constraint/Constraint.cpp
        HttpServer.cpp
        xmonit/OneWireTherm.cpp
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
set(POCO_USE_STATIC_LIBS TRUE)

IF(POCO_USE_STATIC_LIBS)
	#SET(CMAKE_EXE_LINKER_FLAGS "-static")
    SET(POCO_LIB_EXT ".a")
    SET(PROMETHEUS_LIB_EXT ".a")
ELSE()
    SET(POCO_LIB_EXT ".so")
    SET(PROMETHEUS_LIB_EXT ".so")
ENDIF(POCO_USE_STATIC_LIBS)

set(POCO_LIBS 
	"${POCO_LIB_DIR}/libPocoNet${POCO_LIB_EXT}"
        "${POCO_LIB_DIR}/libPocoNetSSL${POCO_LIB_EXT}"
        "${POCO_LIB_DIR}/libPocoUtil${POCO_LIB_EXT}"
        "${POCO_LIB_DIR}/libPocoJSON${POCO_LIB_EXT}"
        "${POCO_LIB_DIR}/libPocoCrypto${POCO_LIB_EXT}"
        "${POCO_LIB_DIR}/libPocoXML${POCO_LIB_EXT}"
        "${POCO_LIB_DIR}/libPocoFoundation${POCO_LIB_EXT}")

set(PROMETHEUS_DEPLOY_DIR "/usr/local")
set(PROMETHEUS_INCLUDE_DIR "${PROMETHEUS_DEPLOY_DIR}/include")
set(PROMETHEUS_LIBS 
        "${PROMETHEUS_DEPLOY_DIR}/lib/libprometheus-cpp-push${PROMETHEUS_LIB_EXT}"
        "${PROMETHEUS_DEPLOY_DIR}/lib/libprometheus-cpp-pull${PROMETHEUS_LIB_EXT}"
	"${PROMETHEUS_DEPLOY_DIR}/lib/libprometheus-cpp-core${PROMETHEUS_LIB_EXT}"
)

target_include_directories(solar-power-mgr PRIVATE "${POCO_INCLUDE_DIR}" "${PROMETHEUS_INCLUDE_DIR}")
target_link_libraries(solar-power-mgr "${POCO_LIBS}" "${PROMETHEUS_LIBS}" "${SSL_LIB}" "${CRYPTO_LIB}" -ldl  Threads::Threads)
