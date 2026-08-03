add_library(RostoNatural MODULE EffectMain.cpp)
target_include_directories(RostoNatural PRIVATE
  "${AE_SDK_ROOT}/Examples/Headers"
  "${AE_SDK_ROOT}/Examples/Headers/SP"
)
target_link_libraries(RostoNatural PRIVATE rosto_core)
target_compile_definitions(RostoNatural PRIVATE MAC_ENV=1)
if(APPLE)
  target_link_libraries(RostoNatural PRIVATE "-framework CoreFoundation")
endif()
set_target_properties(RostoNatural PROPERTIES
  BUNDLE TRUE
  BUNDLE_EXTENSION "plugin"
  OUTPUT_NAME "Rosto Natural"
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
  add_custom_command(TARGET RostoNatural POST_BUILD
    COMMAND "${ROSTO_REZ_EXECUTABLE}"
            -o "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Resources/RostoNaturalPiPL.rsrc"
            -d SystemSevenOrLater=1
            -useDF
            -script Roman
            -d __MACH__
            -d AE_OS_MAC=1
            -arch arm64
            -i "${AE_SDK_ROOT}/Examples/Headers"
            -i "${AE_SDK_ROOT}/Examples/Headers/SP"
            -i "${AE_SDK_ROOT}/Examples/Resources"
            "${CMAKE_CURRENT_SOURCE_DIR}/RostoNaturalPiPL.r"
    COMMAND /usr/bin/test -s
            "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Resources/RostoNaturalPiPL.rsrc"
    VERBATIM
  )
endif()
if(ROSTO_WITH_MEDIAPIPE)
  add_custom_command(TARGET RostoNatural POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Frameworks"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:rosto_mediapipe_bridge>"
            "$<TARGET_BUNDLE_CONTENT_DIR:RostoNatural>/Frameworks/"
  )
endif()

install(TARGETS RostoNatural
  BUNDLE DESTINATION "."
)
