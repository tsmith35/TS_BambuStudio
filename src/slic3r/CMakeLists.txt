cmake_minimum_required(VERSION 3.13)
project(libslic3r_gui)

include(PrecompiledHeader)

set(SLIC3R_GUI_SOURCES
    pchheader.cpp
    pchheader.hpp
    GUI/Printer/PrinterFileSystem.cpp
    GUI/Printer/PrinterFileSystem.h
    GUI/Widgets/AnimaController.hpp
    GUI/Widgets/AnimaController.cpp
    GUI/Widgets/AxisCtrlButton.cpp
    GUI/Widgets/AxisCtrlButton.hpp
    GUI/Widgets/Button.cpp
    GUI/Widgets/Button.hpp
    GUI/Widgets/CheckBox.cpp
    GUI/Widgets/CheckBox.hpp
    GUI/Widgets/RadioBox.hpp
    GUI/Widgets/RadioBox.cpp
    GUI/Widgets/ProgressDialog.hpp
    GUI/Widgets/ProgressDialog.cpp
    GUI/Widgets/RoundedRectangle.hpp
    GUI/Widgets/RoundedRectangle.cpp
    GUI/Widgets/ComboBox.cpp
    GUI/Widgets/ComboBox.hpp
    GUI/Widgets/SideButton.hpp
    GUI/Widgets/SideButton.cpp
    GUI/Widgets/SideMenuPopup.hpp
    GUI/Widgets/SideMenuPopup.cpp
    GUI/Widgets/DropDown.cpp
    GUI/Widgets/DropDown.hpp
    GUI/Widgets/PopupWindow.cpp
    GUI/Widgets/PopupWindow.hpp
    GUI/Widgets/Label.cpp
    GUI/Widgets/Label.hpp
    GUI/Widgets/Scrollbar.cpp
    GUI/Widgets/Scrollbar.hpp
    GUI/Widgets/ScrolledWindow.cpp
    GUI/Widgets/ScrolledWindow.hpp
    GUI/Widgets/StaticBox.cpp
    GUI/Widgets/StaticBox.hpp
    GUI/Widgets/StaticGroup.cpp
    GUI/Widgets/StaticGroup.hpp
    GUI/Widgets/ImageSwitchButton.cpp
    GUI/Widgets/ImageSwitchButton.hpp
    GUI/Widgets/SwitchButton.cpp
    GUI/Widgets/SwitchButton.hpp
    GUI/Widgets/SpinInput.cpp
    GUI/Widgets/SpinInput.hpp
    GUI/Widgets/StaticLine.cpp
    GUI/Widgets/StaticLine.hpp
    GUI/Widgets/StateColor.cpp
    GUI/Widgets/StateColor.hpp
    GUI/Widgets/StateHandler.cpp
    GUI/Widgets/StateHandler.hpp
    GUI/Widgets/TabCtrl.cpp
    GUI/Widgets/TabCtrl.hpp
    GUI/Widgets/TextInput.cpp
    GUI/Widgets/TextInput.hpp
    GUI/Widgets/TempInput.cpp
    GUI/Widgets/TempInput.hpp
    GUI/Widgets/AMSControl.cpp
    GUI/Widgets/AMSControl.hpp
    GUI/Widgets/AMSItem.cpp
    GUI/Widgets/AMSItem.hpp
    GUI/Widgets/FilamentLoad.cpp
    GUI/Widgets/FilamentLoad.hpp
    GUI/Widgets/FanControl.cpp
    GUI/Widgets/FanControl.hpp
    GUI/Widgets/Scrollbar.cpp
    GUI/Widgets/Scrollbar.hpp
    GUI/Widgets/ScrolledWindow.cpp
    GUI/Widgets/ScrolledWindow.hpp
    GUI/Widgets/StepCtrl.cpp
    GUI/Widgets/StepCtrl.hpp
    GUI/Widgets/ProgressBar.cpp
    GUI/Widgets/ProgressBar.hpp
    GUI/Widgets/SideTools.cpp
    GUI/Widgets/SideTools.hpp
    GUI/Widgets/WebView.cpp
    GUI/Widgets/WebView.hpp
    GUI/Widgets/ErrorMsgStaticText.cpp
    GUI/Widgets/ErrorMsgStaticText.hpp
    GUI/AboutDialog.cpp
    GUI/AboutDialog.hpp
    GUI/NetworkTestDialog.cpp
    GUI/NetworkTestDialog.hpp
    GUI/AuxiliaryDialog.cpp
    GUI/AuxiliaryDialog.hpp
    GUI/Auxiliary.cpp
    GUI/Auxiliary.hpp
    GUI/DailyTips.cpp
    GUI/DailyTips.hpp
    GUI/EncodedFilament.hpp
    GUI/EncodedFilament.cpp
    GUI/Project.cpp
    GUI/Project.hpp
    GUI/BackgroundSlicingProcess.cpp
    GUI/BackgroundSlicingProcess.hpp
    GUI/BitmapCache.cpp
    GUI/BitmapCache.hpp
    GUI/ImageGrid.cpp
    GUI/ImageGrid.h
    GUI/3DScene.cpp
    GUI/3DScene.hpp
    GUI/format.hpp
    GUI/GLShadersManager.hpp
    GUI/GLShadersManager.cpp
    GUI/GLShader.cpp
    GUI/GLShader.hpp
    GUI/GLCanvas3D.hpp
    GUI/GLCanvas3D.cpp
    GUI/GLEnums.hpp
    GUI/OpenGLManager.hpp
    GUI/OpenGLManager.cpp
    GUI/Selection.hpp
    GUI/Selection.cpp
    GUI/SlicingProgressNotification.cpp
    GUI/SlicingProgressNotification.hpp
    GUI/Gizmos/GLGizmosManager.cpp
    GUI/Gizmos/GLGizmosManager.hpp
    GUI/Gizmos/GLGizmosCommon.cpp
    GUI/Gizmos/GLGizmosCommon.hpp
    GUI/Gizmos/GLGizmoBase.cpp
    GUI/Gizmos/GLGizmoBase.hpp
    GUI/Gizmos/GLGizmoMove.cpp
    GUI/Gizmos/GLGizmoMove.hpp
    GUI/Gizmos/GLGizmoRotate.cpp
    GUI/Gizmos/GLGizmoRotate.hpp
    GUI/Gizmos/GLGizmoScale.cpp
    GUI/Gizmos/GLGizmoScale.hpp
    GUI/Gizmos/GLGizmoSlaSupports.cpp
    GUI/Gizmos/GLGizmoSlaSupports.hpp
    GUI/Gizmos/GLGizmoFdmSupports.cpp
    GUI/Gizmos/GLGizmoFdmSupports.hpp
    GUI/Gizmos/GLGizmoFlatten.cpp
    GUI/Gizmos/GLGizmoFlatten.hpp
    GUI/Gizmos/GLGizmoAdvancedCut.cpp
    GUI/Gizmos/GLGizmoAdvancedCut.hpp
    GUI/Gizmos/GLGizmoHollow.cpp
    GUI/Gizmos/GLGizmoHollow.hpp
    GUI/Gizmos/GLGizmoPainterBase.cpp
    GUI/Gizmos/GLGizmoPainterBase.hpp
    GUI/Gizmos/GLGizmoSimplify.cpp
    GUI/Gizmos/GLGizmoSimplify.hpp
    GUI/Gizmos/GLGizmoMmuSegmentation.cpp
    GUI/Gizmos/GLGizmoMmuSegmentation.hpp
    GUI/Gizmos/GLGizmoFuzzySkin.cpp
    GUI/Gizmos/GLGizmoFuzzySkin.hpp
    GUI/Gizmos/GLGizmoFaceDetector.cpp
    GUI/Gizmos/GLGizmoFaceDetector.hpp
    GUI/Gizmos/GLGizmoMeasure.cpp
    GUI/Gizmos/GLGizmoMeasure.hpp
    GUI/Gizmos/GLGizmoBrimEars.cpp
    GUI/Gizmos/GLGizmoBrimEars.hpp
    GUI/Gizmos/GLGizmoAssembly.cpp
    GUI/Gizmos/GLGizmoAssembly.hpp
    GUI/Gizmos/GLGizmoSeam.cpp
    GUI/Gizmos/GLGizmoSeam.hpp
    GUI/Gizmos/GLGizmoText.cpp
    GUI/Gizmos/GLGizmoText.hpp
    GUI/Gizmos/GLGizmoSVG.cpp
    GUI/Gizmos/GLGizmoSVG.hpp
    GUI/Gizmos/GLGizmoMeshBoolean.cpp
    GUI/Gizmos/GLGizmoMeshBoolean.hpp
    GUI/GLSelectionRectangle.cpp
    GUI/GLSelectionRectangle.hpp
    GUI/Gizmos/GizmoObjectManipulation.cpp
    GUI/Gizmos/GizmoObjectManipulation.hpp
    GUI/GLModel.hpp
    GUI/GLModel.cpp
    GUI/GLTexture.hpp
    GUI/GLTexture.cpp
    GUI/GLToolbar.hpp
    GUI/GLToolbar.cpp
    GUI/GuiColor.cpp
    GUI/GuiColor.hpp
    GUI/IMToolbar.hpp
    GUI/IMToolbar.cpp
    GUI/GCodeViewer.hpp
    GUI/GCodeViewer.cpp
    GUI/ImageDPIFrame.cpp
    GUI/ImageDPIFrame.hpp
    GUI/Preferences.cpp
    GUI/Preferences.hpp
    GUI/AMSSetting.cpp
    GUI/AMSSetting.hpp
    GUI/AMSMaterialsSetting.cpp
    GUI/AMSMaterialsSetting.hpp
    GUI/ExtrusionCalibration.cpp
    GUI/ExtrusionCalibration.hpp
    GUI/PresetHints.cpp
    GUI/PresetHints.hpp
    GUI/GUI.cpp
    GUI/GUI.hpp
    GUI/GUI_Init.cpp
    GUI/GUI_Init.hpp
    GUI/GUI_Preview.cpp
    GUI/GUI_Preview.hpp
    GUI/GUI_App.cpp
    GUI/GUI_App.hpp
    GUI/GUI_Utils.cpp
    GUI/GUI_Utils.hpp
    GUI/I18N.cpp
    GUI/I18N.hpp
	GUI/IconManager.cpp
    GUI/IconManager.hpp
    GUI/MainFrame.cpp
    GUI/MainFrame.hpp
    GUI/BBLTopbar.cpp
    GUI/BBLTopbar.hpp
    GUI/BedShapeDialog.cpp
    GUI/BedShapeDialog.hpp
    GUI/Plater.cpp
    GUI/Plater.hpp
    GUI/PartPlate.cpp
    GUI/PartPlate.hpp
    GUI/UserNotification.cpp
    GUI/UserNotification.hpp
    GUI/PresetComboBoxes.hpp
    GUI/PresetComboBoxes.cpp
    GUI/BitmapComboBox.hpp
    GUI/BitmapComboBox.cpp
    GUI/SavePresetDialog.hpp
    GUI/SavePresetDialog.cpp
    GUI/GUI_Colors.hpp
    GUI/GUI_Colors.cpp
    GUI/GUI_Factories.cpp
    GUI/GUI_Factories.hpp
    GUI/GUI_ObjectList.cpp
    GUI/GUI_ObjectList.hpp
    GUI/GUI_ObjectLayers.cpp
    GUI/GUI_ObjectLayers.hpp
    GUI/GUI_AuxiliaryList.cpp
    GUI/GUI_AuxiliaryList.hpp
    GUI/GUI_ObjectSettings.cpp
    GUI/GUI_ObjectSettings.hpp
    GUI/GUI_ObjectTable.cpp
    GUI/GUI_ObjectTable.hpp
    GUI/GUI_ObjectTableSettings.cpp
    GUI/GUI_ObjectTableSettings.hpp
    GUI/MeshUtils.cpp
    GUI/MeshUtils.hpp
    GUI/TickCode.cpp
    GUI/TickCode.hpp
    GUI/Tab.cpp
    GUI/Tab.hpp
    GUI/ParamsDialog.cpp
    GUI/ParamsDialog.hpp
    GUI/ParamsPanel.cpp
    GUI/ParamsPanel.hpp
    GUI/PrintHostDialogs.cpp
    GUI/PrintHostDialogs.hpp
    GUI/AmsWidgets.cpp
    GUI/AmsWidgets.hpp
    GUI/MediaFilePanel.cpp
    GUI/MediaFilePanel.h
    GUI/MediaPlayCtrl.cpp
    GUI/MediaPlayCtrl.h
    GUI/MonitorBasePanel.cpp
    GUI/MonitorBasePanel.h
    GUI/UpgradePanel.cpp
    GUI/UpgradePanel.hpp
    GUI/HintNotification.hpp
    GUI/HintNotification.cpp
    GUI/HMSPanel.hpp
    GUI/HMSPanel.cpp
    GUI/MonitorPage.cpp
    GUI/MonitorPage.hpp
    GUI/StatusPanel.cpp
    GUI/StatusPanel.hpp
    GUI/HMS.hpp
    GUI/HMS.cpp
    GUI/SliceInfoPanel.cpp
    GUI/SliceInfoPanel.hpp
    GUI/CameraPopup.cpp
    GUI/CameraPopup.hpp
    GUI/Monitor.cpp
    GUI/Monitor.hpp
    GUI/WebViewDialog.cpp
    GUI/WebViewDialog.hpp
    GUI/PrinterWebView.cpp
    GUI/PrinterWebView.hpp
    GUI/WebDownPluginDlg.hpp
    GUI/WebDownPluginDlg.cpp
    GUI/WebGuideDialog.hpp
    GUI/WebGuideDialog.cpp
    GUI/WebUserLoginDialog.cpp
    GUI/WebUserLoginDialog.hpp
    GUI/ConfigManipulation.cpp
    GUI/ConfigManipulation.hpp
    GUI/Field.cpp
    GUI/Field.hpp
    GUI/DragDropPanel.cpp
    GUI/DragDropPanel.hpp
    GUI/CapsuleButton.cpp
    GUI/CapsuleButton.hpp
    GUI/FilamentMapPanel.cpp
    GUI/FilamentMapPanel.hpp
    GUI/FilamentMapDialog.cpp
    GUI/FilamentMapDialog.hpp
    GUI/OptionsGroup.cpp
    GUI/OptionsGroup.hpp
    GUI/OG_CustomCtrl.cpp
    GUI/OG_CustomCtrl.hpp
    GUI/MarkdownTip.cpp
    GUI/MarkdownTip.hpp
    GUI/2DBed.cpp
    GUI/2DBed.hpp
    GUI/3DBed.cpp
    GUI/3DBed.hpp
    GUI/Camera.cpp
    GUI/Camera.hpp
    GUI/CameraUtils.cpp
    GUI/CameraUtils.hpp
    GUI/wxExtensions.cpp
    GUI/wxExtensions.hpp
    GUI/ObjColorDialog.cpp
    GUI/ObjColorDialog.hpp
    GUI/FilamentPickerDialog.cpp
    GUI/FilamentPickerDialog.hpp
    GUI/FilamentBitmapUtils.cpp
    GUI/FilamentBitmapUtils.hpp
    GUI/WipeTowerDialog.cpp
    GUI/WipeTowerDialog.hpp
    GUI/RemovableDriveManager.cpp
    GUI/RemovableDriveManager.hpp
    GUI/SendSystemInfoDialog.cpp
    GUI/SendSystemInfoDialog.hpp
    GUI/StepMeshDialog.cpp
    GUI/StepMeshDialog.hpp
    GUI/BaseTransparentDPIFrame.cpp
    GUI/BaseTransparentDPIFrame.hpp
    GUI/SyncAmsInfoDialog.cpp
    GUI/SyncAmsInfoDialog.hpp
    GUI/SurfaceDrag.cpp
    GUI/SurfaceDrag.hpp
    GUI/TextLines.cpp
    GUI/TextLines.hpp
    GUI/PlateSettingsDialog.cpp
    GUI/PlateSettingsDialog.hpp
    GUI/ImGuiWrapper.hpp
    GUI/ImGuiWrapper.cpp
    GUI/DeviceManager.hpp
    GUI/DeviceManager.cpp
    GUI/UserManager.hpp
    GUI/UserManager.cpp
    GUI/HttpServer.hpp
    GUI/HttpServer.cpp
    Config/Snapshot.cpp
    Config/Snapshot.hpp
    Config/Version.cpp
    Config/Version.hpp
    Utils/ASCIIFolding.cpp
    Utils/ASCIIFolding.hpp
    GUI/ConfigWizard.cpp
    GUI/ConfigWizard.hpp
    GUI/ConnectPrinter.cpp
    GUI/ConnectPrinter.hpp
    GUI/ConfigWizard_private.hpp
    GUI/MsgDialog.cpp
    GUI/MsgDialog.hpp
    GUI/DownloadProgressDialog.hpp
    GUI/DownloadProgressDialog.cpp
    GUI/UpdateDialogs.cpp
    GUI/UpdateDialogs.hpp
    GUI/Jobs/Job.hpp
    GUI/Jobs/Job.cpp
    GUI/Jobs/JobNew.hpp
    GUI/Jobs/Worker.hpp
    GUI/Jobs/BoostThreadWorker.hpp
    GUI/Jobs/BoostThreadWorker.cpp
    GUI/Jobs/BusyCursorJob.hpp
    GUI/Jobs/ThreadSafeQueue.hpp
    GUI/Jobs/PlaterWorker.hpp
    GUI/Jobs/EmbossJob.cpp
    GUI/Jobs/EmbossJob.hpp
    GUI/Jobs/PlaterJob.hpp
    GUI/Jobs/PlaterJob.cpp
    GUI/Jobs/UpgradeNetworkJob.hpp
    GUI/Jobs/UpgradeNetworkJob.cpp
    GUI/Jobs/ArrangeJob.hpp
    GUI/Jobs/ArrangeJob.cpp
    GUI/Jobs/CreateFontNameImageJob.cpp
    GUI/Jobs/CreateFontNameImageJob.hpp
    GUI/Jobs/CreateFontStyleImagesJob.cpp
    GUI/Jobs/CreateFontStyleImagesJob.hpp
    GUI/Jobs/OrientJob.hpp
    GUI/Jobs/OrientJob.cpp
    GUI/Jobs/RotoptimizeJob.hpp
    GUI/Jobs/RotoptimizeJob.cpp
    GUI/Jobs/FillBedJob.hpp
    GUI/Jobs/FillBedJob.cpp
    GUI/Jobs/SLAImportJob.hpp
    GUI/Jobs/SLAImportJob.cpp
    GUI/Jobs/ProgressIndicator.hpp
    GUI/Jobs/PrintJob.hpp
    GUI/Jobs/PrintJob.cpp
    GUI/Jobs/SendJob.hpp
    GUI/Jobs/SendJob.cpp
    GUI/Jobs/BindJob.hpp
    GUI/Jobs/BindJob.cpp
    GUI/Jobs/NotificationProgressIndicator.hpp
    GUI/Jobs/NotificationProgressIndicator.cpp
    GUI/PhysicalPrinterDialog.hpp
    GUI/PhysicalPrinterDialog.cpp
    GUI/ProgressStatusBar.hpp
    GUI/ProgressStatusBar.cpp
    GUI/BBLStatusBar.hpp
    GUI/BBLStatusBar.cpp
    GUI/BBLStatusBarSend.hpp
    GUI/BBLStatusBarSend.cpp
    GUI/BBLStatusBarPrint.hpp
    GUI/BBLStatusBarPrint.cpp
    GUI/BBLStatusBarBind.hpp
    GUI/BBLStatusBarBind.cpp
    GUI/Mouse3DController.cpp
    GUI/Mouse3DController.hpp
    GUI/IMSlider.cpp
    GUI/IMSlider.hpp
    GUI/IMSlider_Utils.hpp
    GUI/Notebook.cpp
    GUI/Notebook.hpp
    GUI/TabButton.cpp
    GUI/TabButton.hpp
    GUI/Tabbook.cpp
    GUI/Tabbook.hpp
    GUI/ObjectDataViewModel.cpp
    GUI/ObjectDataViewModel.hpp
    GUI/AuxiliaryDataViewModel.cpp
    GUI/AuxiliaryDataViewModel.hpp
    GUI/InstanceCheck.cpp
    GUI/InstanceCheck.hpp
    GUI/Search.cpp
    GUI/Search.hpp
    GUI/FilamentGroupPopup.hpp
    GUI/FilamentGroupPopup.cpp
    GUI/NotificationManager.cpp
    GUI/NotificationManager.hpp
    GUI/UnsavedChangesDialog.cpp
    GUI/UnsavedChangesDialog.hpp
    GUI/UserPresetsDialog.cpp
    GUI/UserPresetsDialog.hpp
    GUI/ExtraRenderers.cpp
    GUI/ExtraRenderers.hpp
    GUI/ProjectDirtyStateManager.hpp
    GUI/ProjectDirtyStateManager.cpp
    GUI/DesktopIntegrationDialog.cpp
    GUI/DesktopIntegrationDialog.hpp
    GUI/DragCanvas.cpp
    GUI/DragCanvas.hpp
    GUI/PublishDialog.cpp
    GUI/PublishDialog.hpp
    GUI/RecenterDialog.cpp
    GUI/RecenterDialog.hpp
    GUI/PrivacyUpdateDialog.cpp
    GUI/PrivacyUpdateDialog.hpp
    GUI/BonjourDialog.cpp
    GUI/BonjourDialog.hpp
    GUI/BindDialog.cpp
    GUI/BindDialog.hpp
    GUI/PartSkipCommon.hpp
    GUI/PartSkipDialog.cpp
    GUI/PartSkipDialog.hpp
    GUI/SkipPartCanvas.cpp
    GUI/SkipPartCanvas.hpp
    GUI/ModelMall.hpp
    GUI/ModelMall.cpp
    GUI/SelectMachine.hpp
    GUI/SelectMachine.cpp
    GUI/PrePrintChecker.hpp
    GUI/PrePrintChecker.cpp
    GUI/SelectMachinePop.hpp
    GUI/SelectMachinePop.cpp
    GUI/SendToPrinter.hpp
    GUI/SendToPrinter.cpp
    GUI/AmsMappingPopup.hpp
    GUI/AmsMappingPopup.cpp
    GUI/ReleaseNote.hpp
    GUI/ReleaseNote.cpp
    GUI/SingleChoiceDialog.hpp
    GUI/SingleChoiceDialog.cpp
    GUI/CaliHistoryDialog.hpp
    GUI/CaliHistoryDialog.cpp
    GUI/CalibrationPanel.hpp
    GUI/CalibrationPanel.cpp
    GUI/CalibrationWizard.hpp
    GUI/CalibrationWizard.cpp
    GUI/CalibrationWizardPage.cpp
    GUI/CalibrationWizardPage.hpp
    GUI/CalibrationWizardStartPage.cpp
    GUI/CalibrationWizardStartPage.hpp
    GUI/CalibrationWizardPresetPage.cpp
    GUI/CalibrationWizardPresetPage.hpp
    GUI/CalibrationWizardCaliPage.cpp
    GUI/CalibrationWizardCaliPage.hpp
    GUI/CalibrationWizardSavePage.cpp
    GUI/CalibrationWizardSavePage.hpp
    GUI/calib_dlg.cpp
    GUI/calib_dlg.hpp
    GUI/Calibration.hpp
    GUI/Calibration.cpp
    GUI/PrintOptionsDialog.hpp
    GUI/PrintOptionsDialog.cpp
    GUI/CreatePresetsDialog.hpp
    GUI/CreatePresetsDialog.cpp
    Utils/json_diff.hpp
    Utils/json_diff.cpp
    GUI/KBShortcutsDialog.hpp
    GUI/KBShortcutsDialog.cpp
    GUI/MultiMachine.hpp
    GUI/MultiMachine.cpp
    GUI/MultiMachinePage.hpp
    GUI/MultiMachinePage.cpp
    GUI/MultiMachineManagerPage.cpp
    GUI/MultiMachineManagerPage.hpp
    GUI/MultiPrintJob.cpp
    GUI/MultiPrintJob.hpp
    GUI/MultiSendMachineModel.hpp
    GUI/MultiSendMachineModel.cpp
    GUI/MultiTaskManagerPage.hpp
    GUI/MultiTaskManagerPage.cpp
    GUI/MultiTaskModel.hpp
    GUI/MultiTaskModel.cpp
    GUI/SendMultiMachinePage.hpp
    GUI/SendMultiMachinePage.cpp
    GUI/TaskManager.cpp
    GUI/TaskManager.hpp
    Utils/WxFontUtils.cpp
    Utils/WxFontUtils.hpp
    Utils/EmbossStyleManager.cpp
    Utils/EmbossStyleManager.hpp
    Utils/Http.cpp
    Utils/Http.hpp
    Utils/FixModelByWin10.cpp
    Utils/FixModelByWin10.hpp
    Utils/Bonjour.cpp
    Utils/Bonjour.hpp
    Utils/BBLUtil.hpp
    Utils/BBLUtil.cpp
    Utils/FileHelp.cpp
    Utils/FileHelp.hpp
    Utils/PresetUpdater.cpp
    Utils/PresetUpdater.hpp
    Utils/Process.cpp
    Utils/Process.hpp
    Utils/RaycastManager.cpp
    Utils/RaycastManager.hpp
    Utils/Profile.hpp
    Utils/UndoRedo.cpp
    Utils/UndoRedo.hpp
    Utils/HexFile.cpp
    Utils/HexFile.hpp
    Utils/TCPConsole.cpp
    Utils/TCPConsole.hpp
    Utils/minilzo_extension.hpp
    Utils/minilzo_extension.cpp
    Utils/ColorSpaceConvert.hpp
    Utils/ColorSpaceConvert.cpp
    Utils/OctoPrint.hpp
    Utils/OctoPrint.cpp
    Utils/PrintHost.hpp
    Utils/PrintHost.cpp
    Utils/NetworkAgent.cpp
    Utils/NetworkAgent.hpp
    Utils/MKS.hpp
    Utils/MKS.cpp
    Utils/CpuMemory.cpp
    Utils/CpuMemory.hpp
    Utils/Duet.cpp
    Utils/Duet.hpp
    Utils/FlashAir.cpp
    Utils/FlashAir.hpp
    Utils/AstroBox.cpp
    Utils/AstroBox.hpp
    Utils/Repetier.cpp
    Utils/Repetier.hpp
    Utils/CalibUtils.cpp
    Utils/CalibUtils.hpp
    Utils/ProfileDescription.hpp
	Utils/FontConfigHelp.cpp
	Utils/FontConfigHelp.hpp
	Utils/FontUtils.cpp
	Utils/FontUtils.hpp
    Utils/WxFontUtils.hpp
    Utils/WxFontUtils.cpp
)

add_subdirectory(GUI/DeviceTab)

if (WIN32)
    list(APPEND SLIC3R_GUI_SOURCES
            GUI/dark_mode/dark_mode.hpp
            GUI/dark_mode/IatHook.hpp
            GUI/dark_mode/UAHMenuBar.hpp
            GUI/dark_mode.hpp
            GUI/dark_mode.cpp
        )
endif ()

if (APPLE)
    list(APPEND SLIC3R_GUI_SOURCES
            Utils/RetinaHelperImpl.mm
            Utils/MacDarkMode.mm
            GUI/RemovableDriveManagerMM.mm
            GUI/RemovableDriveManagerMM.h
            GUI/Mouse3DHandlerMac.mm
            GUI/InstanceCheckMac.mm
            GUI/InstanceCheckMac.h
            GUI/wxMediaCtrl2.mm
            GUI/wxMediaCtrl2.h
            GUI/wxMediaCtrl3.h
        )
    FIND_LIBRARY(DISKARBITRATION_LIBRARY DiskArbitration)
else ()
    list(APPEND SLIC3R_GUI_SOURCES
            GUI/AVVideoDecoder.cpp
            GUI/AVVideoDecoder.hpp
            GUI/wxMediaCtrl2.cpp
            GUI/wxMediaCtrl2.h
            GUI/wxMediaCtrl3.cpp
            GUI/wxMediaCtrl3.h
        )
endif ()

if (UNIX AND NOT APPLE)
    list(APPEND SLIC3R_GUI_SOURCES
        GUI/Printer/gstbambusrc.c
    )
endif ()

add_library(libslic3r_gui STATIC ${SLIC3R_GUI_SOURCES})
target_include_directories(libslic3r_gui PRIVATE Utils)

if (WIN32)
    target_include_directories(libslic3r_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/WebView2/include)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SLIC3R_GUI_SOURCES})

encoding_check(libslic3r_gui)

target_link_libraries(libslic3r_gui libslic3r cereal imgui imguizmo minilzo GLEW::GLEW OpenGL::GL hidapi ${wxWidgets_LIBRARIES} glfw libcurl OpenSSL::SSL OpenSSL::Crypto)
#target_link_libraries(libslic3r_gui libslic3r cereal imgui imguizmo minilzo GLEW::GLEW OpenGL::GL hidapi libcurl OpenSSL::SSL OpenSSL::Crypto ${wxWidgets_LIBRARIES} glfw)

if (MSVC)
    target_link_libraries(libslic3r_gui Setupapi.lib)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    FIND_LIBRARY(WAYLAND_SERVER_LIBRARIES NAMES wayland-server)
    FIND_LIBRARY(WAYLAND_EGL_LIBRARIES    NAMES wayland-egl)
    FIND_LIBRARY(WAYLAND_CLIENT_LIBRARIES NAMES wayland-client)
    find_package(CURL REQUIRED)
    target_link_libraries(libslic3r_gui ${DBUS_LIBRARIES} OSMesa)
    target_link_libraries(libslic3r_gui
        OpenGL::EGL
        ${WAYLAND_SERVER_LIBRARIES}
        ${WAYLAND_EGL_LIBRARIES}
        ${WAYLAND_CLIENT_LIBRARIES}
        ${CURL_LIBRARIES}
    )
elseif (APPLE)
    target_link_libraries(libslic3r_gui ${DISKARBITRATION_LIBRARY})
endif()

if (SLIC3R_STATIC)
    # FIXME: This was previously exported by wx-config but the wxWidgets
    # cmake build forgets this and the build fails in debug mode (or on raspberry release)
    target_compile_definitions(libslic3r_gui PUBLIC -DwxDEBUG_LEVEL=0)
endif()

if (SLIC3R_STATIC AND NOT SLIC3R_STATIC_EXCLUDE_CURL AND UNIX AND NOT APPLE)
    target_compile_definitions(libslic3r_gui PRIVATE OPENSSL_CERT_OVERRIDE)
endif ()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r_gui pchheader.hpp FORCEINCLUDE)
endif ()

if (NOT APPLE)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavcodec
        libswscale
        libavutil
    )
    target_link_libraries(libslic3r_gui PkgConfig::LIBAV)
endif()

# We need to implement some hacks for wxWidgets and touch the underlying GTK
# layer and sub-libraries. This forces us to use the include locations and
# link these libraries.
if (UNIX AND NOT APPLE)
    find_package(GTK${SLIC3R_GTK} REQUIRED)
    target_include_directories(libslic3r_gui PRIVATE ${GTK${SLIC3R_GTK}_INCLUDE_DIRS})
    target_link_libraries(libslic3r_gui ${GTK${SLIC3R_GTK}_LIBRARIES})

    # We add GStreamer for bambu:/// support.
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
    pkg_check_modules(GST_BASE REQUIRED gstreamer-base-1.0)
    target_link_libraries(libslic3r_gui ${GSTREAMER_LIBRARIES} ${GST_BASE_LIBRARIES})
    target_include_directories(libslic3r_gui PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GST_BASE_INCLUDE_DIRS})
endif ()

# Add a definition so that we can tell we are compiling slic3r.
target_compile_definitions(libslic3r_gui PRIVATE SLIC3R_CURRENTLY_COMPILING_GUI_MODULE)
