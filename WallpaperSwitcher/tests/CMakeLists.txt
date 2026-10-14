# Portable unit tests: no Windows APIs, they build and run on any OS.
function(ws_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ws_core)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

ws_add_test(test_fade_math)
ws_add_test(test_crop_core)
ws_add_test(test_playlist)
