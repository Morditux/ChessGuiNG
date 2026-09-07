cmake_minimum_required(VERSION 3.16)
project(ChessGui
        VERSION 1.0.4
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
        Core
        Gui
        Network
        Svg
        Widgets
)

find_package(Qt6 6.8 COMPONENTS Test QUIET)
find_package(Qt6 6.8 COMPONENTS LinguistTools QUIET)

qt_standard_project_setup(REQUIRES 6.8)

set(ONNXRUNTIME_ROOT "" CACHE PATH
        "Optional ONNX Runtime installation prefix")

find_path(ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        HINTS
        "${ONNXRUNTIME_ROOT}/include"
        /usr/include
        /usr/local/include
        PATH_SUFFIXES onnxruntime
)
find_library(ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        HINTS
        "${ONNXRUNTIME_ROOT}/lib"
        "${ONNXRUNTIME_ROOT}/lib64"
        /usr/lib
        /usr/local/lib
)

if (WIN32)
    find_file(ONNXRUNTIME_RUNTIME_LIBRARY
            NAMES onnxruntime.dll
            HINTS
            "${ONNXRUNTIME_ROOT}/lib"
            "${ONNXRUNTIME_ROOT}/bin"
    )
endif()

if (NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY
        OR (WIN32 AND NOT ONNXRUNTIME_RUNTIME_LIBRARY))
    message(FATAL_ERROR
            "ONNX Runtime development files not found. Install libonnxruntime-dev "
            "or set ONNXRUNTIME_ROOT to its installation prefix.")
endif()

add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
if (WIN32)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
            IMPORTED_LOCATION "${ONNXRUNTIME_RUNTIME_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
    )
else()
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
    )
endif()

set(CHESSGUI_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/chess-tiles-v2.onnx")
set(CHESSGUI_BUILD_MODEL "${CMAKE_CURRENT_BINARY_DIR}/model/chess-tiles-v2.onnx")
configure_file("${CHESSGUI_MODEL}" "${CHESSGUI_BUILD_MODEL}" COPYONLY)

set(app_icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/chessgui.png")
set_source_files_properties("${app_icon}" PROPERTIES
        QT_RESOURCE_ALIAS "icons/chessgui.png"
)

set(opening_book "${CMAKE_CURRENT_SOURCE_DIR}/assets/books/book.bin")
set_source_files_properties("${opening_book}" PROPERTIES
        QT_RESOURCE_ALIAS "books/book.bin"
)

# ---------------------------------------------------------------------------
# Object libraries. Every source file is compiled exactly once and the object
# files are embedded into the application and the test executables. Object
# libraries (rather than static archives) guarantee that Qt resource objects
# (opening book, pieces) are always linked, because all objects are included.
# ---------------------------------------------------------------------------

# chesscore: UI-independent game logic, Qt Core and Network.
add_library(chesscore OBJECT
        appconfig.cpp
        appconfig.h
        book.cpp
        book.h
        computergamesettings.cpp
        computergamesettings.h
        craftyhashkeys.h
        enginebackend.cpp
        enginebackend.h
        gamecontroller.cpp
        gamecontroller.h
        gatewayclient.cpp
        gatewayclient.h
        heuristiceval.cpp
        heuristiceval.h
        pgnannotations.cpp
        pgnannotations.h
        pgnfile.cpp
        pgnfile.h
        rules.cpp
        rules.h
        uciengine.cpp
        uciengine.h
        uciparser.cpp
        uciparser.h
)
target_include_directories(chesscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chesscore PUBLIC Qt6::Core Qt6::Gui Qt6::Network)

# The opening book resource is embedded once here and is available to every
# target linking chesscore.
qt_add_resources(chesscore "ChessGui_core_book_resources"
        PREFIX "/ChessGui"
        FILES
        "${opening_book}"
)

# chessvision: screenshot board detection and FEN recognition, ONNX Runtime.
add_library(chessvision OBJECT
        boarddetector.cpp
        boarddetector.h
        fenrecognizer.cpp
        fenrecognizer.h
        visionworker.cpp
        visionworker.h
)
target_include_directories(chessvision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chessvision PUBLIC Qt6::Gui onnxruntime::onnxruntime)

# chessui: reusable widgets.
# Generate the legacy .qrc explicitly because Visual Studio multi-config
# generators can lose AUTORCC object dependencies for OBJECT libraries.
set(chessui_resources)
qt_add_resources(chessui_resources
        "${CMAKE_CURRENT_SOURCE_DIR}/pieces.qrc"
)

add_library(chessui OBJECT
        chessboard.cpp
        chessboard.h
        chessboard.ui
        engineoutputwidget.cpp
        engineoutputwidget.h
        evaluationbar.cpp
        evaluationbar.h
        movelistwidget.cpp
        movelistwidget.h
        pendulumwidget.cpp
        pendulumwidget.h
)
target_sources(chessui PRIVATE ${chessui_resources})
target_include_directories(chessui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chessui PUBLIC Qt6::Widgets Qt6::Svg chesscore)

# chessapp: main window and dialogs.
add_library(chessapp OBJECT
        computergamedialog.cpp
        computergamedialog.h
        engineconfigurationdialog.cpp
        engineconfigurationdialog.h
        mainwindow.cpp
        mainwindow.h
        pgnselectdialog.cpp
        pgnselectdialog.h
        remoteenginedialog.cpp
        remoteenginedialog.h
        ucioptionsdialog.cpp
        ucioptionsdialog.h
)
target_include_directories(chessapp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chessapp PUBLIC Qt6::Network Qt6::Widgets chesscore chessui chessvision)
target_compile_definitions(chessapp PRIVATE CHESSGUI_SOURCE_MODEL_PATH="${CHESSGUI_MODEL}")

qt_add_executable(ChessGui main.cpp)
target_link_libraries(ChessGui PRIVATE chessapp chesscore chessui chessvision)

qt_add_resources(ChessGui "ChessGui_app_resources"
        PREFIX "/ChessGui"
        FILES
        "${app_icon}"
)

# Internationalization: source strings are English and every user-visible
# string goes through tr(). Translations are optional: when a translations/
# directory with .ts files exists and Qt Linguist tools are available, the
# .qm files are compiled and embedded into the executable.
if (TARGET Qt6::LinguistTools AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/translations")
    file(GLOB CHESSGUI_TRANSLATIONS "${CMAKE_CURRENT_SOURCE_DIR}/translations/*.ts")
    if (CHESSGUI_TRANSLATIONS)
        qt_add_translations(ChessGui
                TS_FILES ${CHESSGUI_TRANSLATIONS}
                SOURCES
                main.cpp
                appconfig.cpp appconfig.h
                book.cpp book.h
                computergamedialog.cpp computergamedialog.h
                computergamesettings.cpp computergamesettings.h
                engineconfigurationdialog.cpp engineconfigurationdialog.h
                enginebackend.cpp enginebackend.h
                gamecontroller.cpp gamecontroller.h
                gatewayclient.cpp gatewayclient.h
                heuristiceval.cpp heuristiceval.h
                pgnfile.cpp pgnfile.h
                rules.cpp rules.h
                uciengine.cpp uciengine.h
                uciparser.cpp uciparser.h
                chessboard.cpp chessboard.h
                engineoutputwidget.cpp engineoutputwidget.h
                evaluationbar.cpp evaluationbar.h
                movelistwidget.cpp movelistwidget.h
                pendulumwidget.cpp pendulumwidget.h
                boarddetector.cpp boarddetector.h
                fenrecognizer.cpp fenrecognizer.h
                visionworker.cpp visionworker.h
                mainwindow.cpp mainwindow.h
                pgnselectdialog.cpp pgnselectdialog.h
                remoteenginedialog.cpp remoteenginedialog.h
                ucioptionsdialog.cpp ucioptionsdialog.h
        )
    endif()
endif()

install(TARGETS ChessGui
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES "${CHESSGUI_MODEL}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/model
)
if (WIN32)
    install(FILES "${ONNXRUNTIME_RUNTIME_LIBRARY}"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
install(FILES LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/chessgui
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES packaging/chessgui.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES "${app_icon}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
            RENAME chessgui.png
    )
    install(FILES assets/pieces/LICENSE-2.0.txt
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/chessgui
            RENAME LICENSE-CHESSNUT.txt
    )

    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "chessgui")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern chess GUI with engine analysis")
    set(CPACK_PACKAGE_CONTACT "ChessGui contributors")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ChessGui contributors")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    include(CPack)
endif()

if (TARGET Qt6::Test AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()

    qt_add_executable(EvaluationBarTest
            tests/evaluationbar_test.cpp
    )
    target_link_libraries(EvaluationBarTest PRIVATE chessui chesscore Qt6::Test)
    add_test(NAME EvaluationBarTest COMMAND EvaluationBarTest)

    qt_add_executable(PendulumWidgetTest
            tests/pendulumwidget_test.cpp
    )
    target_link_libraries(PendulumWidgetTest PRIVATE chessui chesscore Qt6::Test)
    add_test(NAME PendulumWidgetTest COMMAND PendulumWidgetTest)

    qt_add_executable(MoveListWidgetTest
            tests/movelistwidget_test.cpp
    )
    target_link_libraries(MoveListWidgetTest PRIVATE chessui chesscore Qt6::Test)
    add_test(NAME MoveListWidgetTest COMMAND MoveListWidgetTest)

    qt_add_executable(HeuristicEvalTest
            tests/heuristiceval_test.cpp
    )
    target_link_libraries(HeuristicEvalTest PRIVATE chesscore Qt6::Test)
    add_test(NAME HeuristicEvalTest COMMAND HeuristicEvalTest)

    qt_add_executable(UciParserTest
            tests/uciparser_test.cpp
    )
    target_link_libraries(UciParserTest PRIVATE chesscore Qt6::Test)
    add_test(NAME UciParserTest COMMAND UciParserTest)

    qt_add_executable(EngineBackendTest
            tests/enginebackend_test.cpp
    )
    target_link_libraries(EngineBackendTest PRIVATE chesscore Qt6::Test)
    add_test(NAME EngineBackendTest COMMAND EngineBackendTest)

    qt_add_executable(UciEngineTest
            tests/uciengine_test.cpp
    )
    target_link_libraries(UciEngineTest PRIVATE chessapp chesscore chessui chessvision Qt6::Test)
    add_test(NAME UciEngineTest COMMAND UciEngineTest)

    qt_add_executable(EngineOutputWidgetTest
            tests/engineoutputwidget_test.cpp
    )
    target_link_libraries(EngineOutputWidgetTest PRIVATE chessapp chesscore chessui chessvision Qt6::Test)
    add_test(NAME EngineOutputWidgetTest COMMAND EngineOutputWidgetTest)

    qt_add_executable(AppConfigTest
            tests/appconfig_test.cpp
    )
    target_link_libraries(AppConfigTest PRIVATE chessapp chesscore chessui chessvision Qt6::Test)
    add_test(NAME AppConfigTest COMMAND AppConfigTest)

    qt_add_executable(RulesTest
            tests/rules_test.cpp
    )
    target_link_libraries(RulesTest PRIVATE chessapp chesscore chessui chessvision Qt6::Test)
    add_test(NAME RulesTest COMMAND RulesTest)

    qt_add_executable(PgnFileTest
            tests/pgnfile_test.cpp
    )
    target_link_libraries(PgnFileTest PRIVATE chesscore Qt6::Test)
    add_test(NAME PgnFileTest COMMAND PgnFileTest)

    qt_add_executable(BookTest
            tests/book_test.cpp
    )
    target_link_libraries(BookTest PRIVATE chesscore Qt6::Test)
    add_test(NAME BookTest COMMAND BookTest)

    qt_add_executable(GameControllerTest
            tests/gamecontroller_test.cpp
    )
    target_link_libraries(GameControllerTest PRIVATE chesscore Qt6::Test)
    add_test(NAME GameControllerTest COMMAND GameControllerTest)

    qt_add_executable(GatewayClientTest
            tests/gatewayclient_test.cpp
    )
    target_link_libraries(GatewayClientTest PRIVATE chesscore Qt6::Test)
    add_test(NAME GatewayClientTest COMMAND GatewayClientTest)

    qt_add_executable(VisionTest
            tests/vision_test.cpp
    )
    target_compile_definitions(VisionTest PRIVATE CHESSGUI_SOURCE_MODEL_PATH="${CHESSGUI_MODEL}")
    target_link_libraries(VisionTest PRIVATE chessvision Qt6::Test)
    add_test(NAME VisionTest COMMAND VisionTest)

    qt_add_executable(RemoteEngineDialogTest
            tests/remoteenginedialog_test.cpp
    )
    target_link_libraries(RemoteEngineDialogTest PRIVATE chessapp chesscore chessui chessvision Qt6::Test)
    add_test(NAME RemoteEngineDialogTest COMMAND RemoteEngineDialogTest)

    qt_add_executable(UciOptionsDialogTest
            tests/ucioptionsdialog_test.cpp
    )
    target_link_libraries(UciOptionsDialogTest PRIVATE chessapp chesscore chessui chessvision Qt6::Test)
    add_test(NAME UciOptionsDialogTest COMMAND UciOptionsDialogTest)

    qt_add_executable(ChessBoardTest
            tests/chessboard_test.cpp
    )
    target_link_libraries(ChessBoardTest PRIVATE chessui chesscore Qt6::Test)
    add_test(NAME ChessBoardTest COMMAND ChessBoardTest)
endif()
