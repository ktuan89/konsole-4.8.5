project(Konsole)

find_package(KDE4 4.6.40 REQUIRED)

include(KDE4Defaults)

add_definitions(${QT_DEFINITIONS} ${KDE4_DEFINITIONS})
add_definitions(-DQT_USE_FAST_CONCATENATION -DQT_USE_FAST_OPERATOR_PLUS)
add_definitions(-DKDE_DEFAULT_DEBUG_AREA=1211)
include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${KDE4_INCLUDES})

KDE4_NO_ENABLE_FINAL(konsole)

add_subdirectory( src ) 
add_subdirectory( data ) 
add_subdirectory( desktop )
add_subdirectory( doc/manual )

macro_display_feature_log()
