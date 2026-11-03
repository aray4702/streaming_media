# SmCall, the iOS calling app (C14, #1116): SwiftUI screens and an Objective-C++ bridge (CallBridge.mm) to the C++
# call controller (apps/call). Built by the `ios` / `ios-simulator` presets (Xcode generator) as an app bundle without
# signing; the Owner signs it with an Apple ID for a real iPhone (README.md).
enable_language(Swift)
add_executable(SmCall MACOSX_BUNDLE SmCallApp.swift ContentView.swift CallBridge.h CallBridge.mm RemoteVideoView.h
               RemoteVideoView.mm)
set_source_files_properties(CallBridge.mm RemoteVideoView.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(SmCall PRIVATE sm_call_client sm::device "-framework UIKit" "-framework CoreGraphics"
                      "-framework Metal" "-framework MetalKit" "-framework QuartzCore")
set_target_properties(SmCall PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  MACOSX_BUNDLE_GUI_IDENTIFIER io.github.aray4702.smcall
  MACOSX_BUNDLE_BUNDLE_NAME SmCall
  MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1
  MACOSX_BUNDLE_BUNDLE_VERSION 1
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER io.github.aray4702.smcall
  XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/SmCall-Bridging-Header.h
  XCODE_ATTRIBUTE_SWIFT_VERSION 5.0
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
# Signing: unsigned by default (CI, the simulator). For a real iPhone, configure with
# -DSM_IOS_DEVELOPMENT_TEAM=<team ID> (an Apple ID's Personal Team is enough; the ID is the OU of the
# "Apple Development" certificate Xcode creates) and Xcode signs automatically with that team.
set(SM_IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple development team ID that signs the iOS app; empty = unsigned")
if(SM_IOS_DEVELOPMENT_TEAM)
  set_target_properties(SmCall PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${SM_IOS_DEVELOPMENT_TEAM}"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED YES)
  # The project's other iOS executables (command-line tools, built as bundles on iOS) sign with the same team and get
  # a bundle id, so Xcode lists no signing issues for them.
  foreach(tool sm_ai_server sm_testbench sm_call_smoke)
    if(TARGET ${tool})
      string(REPLACE "_" "-" tool_id ${tool})  # a bundle id allows letters, digits, '-' and '.'
      set_target_properties(${tool} PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${SM_IOS_DEVELOPMENT_TEAM}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER io.github.aray4702.${tool_id})
    endif()
  endforeach()
else()
  set_target_properties(SmCall PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Manual)
endif()
