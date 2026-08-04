add_library(RostoNatural MODULE EffectMain.cpp)
target_include_directories(RostoNatural PRIVATE
  "${AE_SDK_ROOT}/Examples/Headers"
  "${AE_SDK_ROOT}/Examples/Headers/SP"
  "${AE_SDK_ROOT}/Examples/Util"
)
target_link_libraries(RostoNatural PRIVATE rosto_core)
target_compile_definitions(RostoNatural PRIVATE MAC_ENV=1)
if(APPLE)
  target_link_options(RostoNatural PRIVATE
    "LINKER:-exported_symbol,_EffectMain"
    "LINKER:-exported_symbol,_PluginDataEntryFunction2"
  )
  target_link_libraries(RostoNatural PRIVATE "-framework CoreFoundation")
endif()
set_target_properties(RostoNatural PROPERTIES
  BUNDLE TRUE
  BUNDLE_EXTENSION "plugin"
  PREFIX ""
  OUTPUT_NAME "Rosto Natural"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  BUILD_WITH_INSTALL_RPATH TRUE
  INSTALL_RPATH "@loader_path/../Frameworks"
)
add_custom_command(TARGET RostoNatural POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Resources"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_SOURCE_DIR}/models/face_landmarker.task"
          "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Resources/face_landmarker.task"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_SOURCE_DIR}/models/hair_segmenter.tflite"
          "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Resources/hair_segmenter.tflite"
)
if(APPLE)
  find_program(ROSTO_REZ_EXECUTABLE Rez REQUIRED)
  set(ROSTO_PIPL_RESOURCE
      "${CMAKE_CURRENT_BINARY_DIR}/RostoNaturalPiPL.rsrc")
  add_custom_command(OUTPUT "${ROSTO_PIPL_RESOURCE}"
    COMMAND "${ROSTO_REZ_EXECUTABLE}"
            -o "${ROSTO_PIPL_RESOURCE}"
            -d SystemSevenOrLater=1
            -useDF
            -script Roman
            -d __MACH__
            -arch arm64
            -i "${AE_SDK_ROOT}/Examples/Headers"
            -i "${AE_SDK_ROOT}/Examples/Headers/SP"
            -i "${AE_SDK_ROOT}/Examples/Resources"
            -i "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/RostoNaturalPiPL.r"
    COMMAND /bin/test -s
            "${ROSTO_PIPL_RESOURCE}"
    DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/RostoNaturalPiPL.r"
            "${CMAKE_CURRENT_SOURCE_DIR}/RostoNaturalVersion.h"
    VERBATIM
  )
  add_custom_target(RostoNaturalPiPL ALL DEPENDS "${ROSTO_PIPL_RESOURCE}")
  add_dependencies(RostoNatural RostoNaturalPiPL)
  add_custom_command(TARGET RostoNatural POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ROSTO_PIPL_RESOURCE}"
            "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Resources/RostoNaturalPiPL.rsrc"
    VERBATIM
  )
endif()
if(ROSTO_WITH_MEDIAPIPE)
  get_filename_component(ROSTO_NATIVE_DEP_DIR
    "${ROSTO_MEDIAPIPE_BRIDGE_LIBRARY}" DIRECTORY)
  set(ROSTO_OPENCV_NAMES
    libopencv_calib3d.3.4.dylib
    libopencv_core.3.4.dylib
    libopencv_features2d.3.4.dylib
    libopencv_highgui.3.4.dylib
    libopencv_imgcodecs.3.4.dylib
    libopencv_imgproc.3.4.dylib
    libopencv_video.3.4.dylib
    libopencv_videoio.3.4.dylib
  )
  set(ROSTO_OPENCV_DYLIBS)
  foreach(name IN LISTS ROSTO_OPENCV_NAMES)
    set(path "${ROSTO_NATIVE_DEP_DIR}/${name}")
    if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
      message(FATAL_ERROR "Required native dependency missing: ${path}")
    endif()
    file(SIZE "${path}" size)
    if(size EQUAL 0)
      message(FATAL_ERROR "Required native dependency is empty: ${path}")
    endif()
    list(APPEND ROSTO_OPENCV_DYLIBS "${path}")
  endforeach()
  add_custom_command(TARGET RostoNatural POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Frameworks"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:rosto_mediapipe_bridge>"
            "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Frameworks/"
    VERBATIM
  )
  foreach(dylib IN LISTS ROSTO_OPENCV_DYLIBS)
    add_custom_command(TARGET RostoNatural POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${dylib}"
              "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Frameworks/"
      VERBATIM
    )
  endforeach()
endif()

set(ROSTO_ADAPTER_DEPENDENCIES
  "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  "${CMAKE_CURRENT_SOURCE_DIR}/RostoNaturalPiPL.r"
  "${CMAKE_CURRENT_SOURCE_DIR}/RostoNaturalVersion.h"
  "${CMAKE_SOURCE_DIR}/models/face_landmarker.task"
  "${CMAKE_SOURCE_DIR}/models/hair_segmenter.tflite"
)
if(ROSTO_WITH_MEDIAPIPE)
  list(APPEND ROSTO_ADAPTER_DEPENDENCIES
    "${ROSTO_MEDIAPIPE_BRIDGE_LIBRARY}"
    ${ROSTO_OPENCV_DYLIBS}
  )
endif()
set_property(SOURCE EffectMain.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${ROSTO_ADAPTER_DEPENDENCIES}")

install(TARGETS RostoNatural
  BUNDLE DESTINATION "."
  LIBRARY DESTINATION "."
)
