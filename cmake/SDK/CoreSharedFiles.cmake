# モジュールへ公開する引擎の共用の核（HIKARI::CoreShared）の一覧。SDK の構成（cmake/HikariSdk.cmake）と
# 写しの手順（StageCoreShared.cmake）が同じ一覧を読む。
# ヘッダーは HIKARI/Core/Public、ソースは HIKARI/Core/Private からの相対路径。引擎のライブラリ無しで完結する
# ものに限る：ここに無い Core のヘッダーを include すると StageCoreShared.cmake が構築を止める。
set(HIKARI_SDK_CORE_SHARED_HEADERS
    "Core/HIKARI_JsonRead.h"
    "Core/HIKARI_MathConfig.h"
    "Core/Math/HIKARI_Math3D.h"
    "Core/Math/HIKARI_ScalarMath.h"
    "Core/Serialization/Json/HIKARI_JsonFieldInfo.h"
    "Core/Serialization/Json/HIKARI_JsonFieldOptions.h"
    "Core/Serialization/Json/HIKARI_JsonFields.h"
    "Core/Text/HIKARI_EnumNames.h"
    "Core/Text/HIKARI_HumanizeKey.h")
set(HIKARI_SDK_CORE_SHARED_SOURCES
    "Core/Math/HIKARI_Math3D.cpp")
