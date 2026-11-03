# SmCall, the iOS calling app (C14, #1116): SwiftUI screens and an Objective-C++ bridge (CallBridge.mm) to the C++
# call controller (apps/call). Built by the `ios` / `ios-simulator` presets (Xcode generator) as an app bundle without
# signing; the Owner signs it with an Apple ID for a real iPhone (README.md).
enable_language(Swift)
add_executable(SmCall MACOSX_BUNDLE SmCallApp.swift ContentView.swift CallBridge.h CallBridge.mm)
set_source_files_properties(CallBridge.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(SmCall PRIVATE sm_call_client sm::device "-framework UIKit" "-framework CoreGraphics")
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
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
  XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE Manual)
