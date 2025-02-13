add_executable(hs-wrap-demo main.cpp)
target_include_directories(hs-wrap-demo PUBLIC ${QT5_INCLUDE_DIRS} ${HS_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/src/)
target_link_libraries(hs-wrap-demo PUBLIC ${QT5_LIBRARIES} ${HS_LIBRARIES} hs-wrap)
#set_target_properties(hs-wrap-demo PROPERTIES LINK_FLAGS -Wl,-rpath=${JAVA_HOME}/lib/server)
