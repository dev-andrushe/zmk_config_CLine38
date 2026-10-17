if(CONFIG_CLINE38_MIXED_SCROLL)
  target_sources(app PRIVATE src/input_processor_mixed_scroll.c)
endif()

if(CONFIG_CLINE38_SLOW_ZOOM)
  target_sources(app PRIVATE src/input_processor_slow_zoom.c)
endif()

if(CONFIG_CLINE38_RETRO_POINTER)
  include(cmake/retro_pointer.cmake)
endif()
