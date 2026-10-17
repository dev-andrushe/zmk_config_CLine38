# Replace the two app sources only after the root ZMK CMakeLists has added them.
# No edits to west checkouts, no dependency fork, no fabricated key positions.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(retro_output "${CMAKE_CURRENT_BINARY_DIR}/cline38-retro")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_retro_pointer.py"
    --app "${APPLICATION_SOURCE_DIR}" --output "${retro_output}"
  RESULT_VARIABLE retro_result
  ERROR_VARIABLE retro_error
)
if(NOT retro_result EQUAL 0)
  message(FATAL_ERROR "CLine38 Retro Tap source adaptation failed: ${retro_error}")
endif()
set_property(GLOBAL PROPERTY CLINE38_RETRO_OUTPUT "${retro_output}")
set_property(GLOBAL PROPERTY CLINE38_RETRO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY "${APPLICATION_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_retro_pointer.py"
  "${APPLICATION_SOURCE_DIR}/src/behaviors/behavior_hold_tap.c"
  "${APPLICATION_SOURCE_DIR}/src/pointing/input_listener.c"
)

function(cline38_replace_retro_sources)
  get_property(output GLOBAL PROPERTY CLINE38_RETRO_OUTPUT)
  get_property(root GLOBAL PROPERTY CLINE38_RETRO_ROOT)
  get_target_property(sources app SOURCES)
  set(hold_count 0)
  set(listener_count 0)
  set(kept)
  foreach(source IN LISTS sources)
    get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${APPLICATION_SOURCE_DIR}")
    if(path STREQUAL "${APPLICATION_SOURCE_DIR}/src/behaviors/behavior_hold_tap.c")
      math(EXPR hold_count "${hold_count} + 1")
    elseif(path STREQUAL "${APPLICATION_SOURCE_DIR}/src/pointing/input_listener.c")
      math(EXPR listener_count "${listener_count} + 1")
    else()
      list(APPEND kept "${source}")
    endif()
  endforeach()
  if(NOT hold_count EQUAL 1 OR NOT listener_count EQUAL 1)
    message(FATAL_ERROR "CLine38 expected exactly one Hold-Tap and input-listener source")
  endif()
  set_property(TARGET app PROPERTY SOURCES "${kept}")
  target_sources(app PRIVATE "${output}/behavior_hold_tap.c" "${output}/input_listener.c")
  target_include_directories(app PRIVATE "${root}/include" "${root}/src")
  message(STATUS "CLine38 Retro Tap: physical ball motion resolves holds before layer routing")
endfunction()

cmake_language(DEFER DIRECTORY "${APPLICATION_SOURCE_DIR}" CALL cline38_replace_retro_sources)
