if(CONFIG_CLINE38_MIXED_SCROLL)
  target_sources(app PRIVATE src/input_processor_mixed_scroll.c)
endif()

if(CONFIG_CLINE38_SLOW_ZOOM)
  target_sources(app PRIVATE src/input_processor_slow_zoom.c)
  if(CONFIG_ZMK_INPUT_PROCESSOR_SCROLL_INERTIA)
    include(cmake/slow_zoom_inertia.cmake)
  endif()
endif()

if(CONFIG_CLINE38_RETRO_POINTER)
  include(cmake/retro_pointer.cmake)
endif()

if(CONFIG_CLINE38_CTRL_LAYER8)
  target_sources(app PRIVATE src/behavior_ctrl_layer8.c)
  include(cmake/ctrl_layer8.cmake)
endif()
