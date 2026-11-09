# 地形と布景モジュール（TerrainKit）。外部モジュールの規則で書く：エンジン本体のライブラリにはリンクせず、
# Core/Public のヘッダーだけの部分（項目表、数学型、名前表）と ThirdParty だけを使う。
# 後で独立したリポジトリ（D:/PJ/HIKARI_TerrainKit）へそのまま移す。

# 地形データ核：タイルの高さ場・制御図・層スタックの評価・筆刷・侵蝕・セルファイル・タイル単位の撤销。
add_library(hikari_terrainkit_data STATIC)
target_sources(
    hikari_terrainkit_data
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainData.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataBrush.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataCodec.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataEdit.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataErosion.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataEvaluate.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataFile.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataGrid.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataImage.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataLayers.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataNoise.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataRules.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataSpline.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Private/HIKARI_TerrainDataStamp.cpp")
target_include_directories(
    hikari_terrainkit_data
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Public"
        "${HIKARI_SOURCE_ROOT}/HIKARI/Core/Public")
target_link_libraries(
    hikari_terrainkit_data
    PUBLIC
        HIKARI::Nlohmann
    PRIVATE
        HIKARI::FastNoise2
        HIKARI::TinyExr)
hikari_configure_engine_target(hikari_terrainkit_data)
set_target_properties(hikari_terrainkit_data PROPERTIES FOLDER "Modules/TerrainKit")
add_library(HIKARI::TerrainKitData ALIAS hikari_terrainkit_data)

# 行動テスト（製品の目標は依存しない）。tests 目録へ出し、ctest -L terrain で走る。
add_executable(HIKARI_TerrainDataContracts EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/Tests/HIKARI_TerrainDataContracts.cpp")
set_target_properties(HIKARI_TerrainDataContracts PROPERTIES
    HIKARI_KEEP_ASSERTIONS ON
    FOLDER "Tests/terrain"
    EXCLUDE_FROM_DEFAULT_BUILD TRUE)
target_link_libraries(HIKARI_TerrainDataContracts PRIVATE HIKARI::TerrainKitData HIKARI::TinyExr)
hikari_configure_engine_target(HIKARI_TerrainDataContracts)
target_compile_definitions(HIKARI_TerrainDataContracts PRIVATE
    HIKARI_CONTRACT_OUTPUT_ROOT="${CMAKE_BINARY_DIR}/tests")
foreach(configuration IN ITEMS Debug EditorRelease Shipping)
    string(TOUPPER "${configuration}" config)
    foreach(kind IN ITEMS ARCHIVE RUNTIME PDB COMPILE_PDB)
        set_target_properties(HIKARI_TerrainDataContracts PROPERTIES
            "${kind}_OUTPUT_DIRECTORY_${config}" "${CMAKE_BINARY_DIR}/tests")
    endforeach()
endforeach()
add_test(NAME HIKARI_TerrainDataContracts COMMAND "$<TARGET_FILE:HIKARI_TerrainDataContracts>")
set_tests_properties(HIKARI_TerrainDataContracts PROPERTIES LABELS "terrain" TIMEOUT 120
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

# モジュール本体の DLL。実行と編集器の入口を一つの DLL に持つ（Shipping は編集器の分を除く）。
# 公開の SDK ヘッダーと host の出力する符号だけを使い、引擎の内部ライブラリには繋がない。
if(TARGET HIKARI_EditorHost)
    add_library(HIKARI_TerrainKit MODULE)
    target_sources(
        HIKARI_TerrainKit
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitGpuErosion.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitGpuErosion.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitModule.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitRenderData.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitRenderData.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitFrustum.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitRivers.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitRivers.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitRuleBaker.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitRuleBaker.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitStore.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitStore.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitSurfaces.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/HIKARI_TerrainKitSurfaces.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/Editor/HIKARI_TerrainKitEditor.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Editor/HIKARI_TerrainKitEditorCommon.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/Editor/HIKARI_TerrainKitExport.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Editor/HIKARI_TerrainKitLayerActions.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Editor/HIKARI_TerrainKitOperations.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Editor/HIKARI_TerrainKitPanel.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Editor/HIKARI_TerrainKitSplines.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/Editor/HIKARI_TerrainKitTools.cpp"
            "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectModuleAdapter.cpp"
            "${HIKARI_SOURCE_ROOT}/cmake/SDK/HIKARI_GameProjectEditorModuleAdapter.cpp")
    target_include_directories(
        HIKARI_TerrainKit
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/Runtime"
            "${HIKARI_SOURCE_ROOT}/include")
    target_compile_definitions(
        HIKARI_TerrainKit
        PRIVATE
            HIKARI_GAME_PROJECT_SDK_IMPORTS=1
            HIKARI_GAME_PROJECT_EDITOR_SDK_IMPORTS=1)
    target_link_libraries(HIKARI_TerrainKit PRIVATE HIKARI_EditorHost HIKARI::TerrainKitData HIKARI::DirectXTex)
    hikari_configure_engine_target(HIKARI_TerrainKit)
    set(_terrainkit_output "$<TARGET_FILE_DIR:HIKARI_EditorHost>/Modules/HIKARI.TerrainKit")
    foreach(configuration IN ITEMS Debug EditorRelease Shipping)
        string(TOUPPER "${configuration}" config)
        foreach(kind IN ITEMS LIBRARY RUNTIME PDB)
            set_target_properties(HIKARI_TerrainKit PROPERTIES
                "${kind}_OUTPUT_DIRECTORY_${config}" "${_terrainkit_output}")
        endforeach()
    endforeach()
    set_target_properties(HIKARI_TerrainKit PROPERTIES
        PREFIX ""
        OUTPUT_NAME "HIKARI_TerrainKit"
        FOLDER "Modules/TerrainKit")
    add_custom_command(
        TARGET HIKARI_TerrainKit
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/HIKARI.TerrainKit.hmodule"
            "${_terrainkit_output}/HIKARI.TerrainKit.hmodule"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
            "${_terrainkit_output}/Shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Content"
            "${_terrainkit_output}/Content"
        VERBATIM)
endif()
