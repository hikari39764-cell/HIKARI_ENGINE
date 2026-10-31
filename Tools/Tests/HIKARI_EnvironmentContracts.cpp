#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <DirectXPackedVector.h>

#include "Assets/Color/HIKARI_ColorLutAsset.h"
#include "Assets/Environment/HIKARI_EnvironmentAsset.h"
#include "Assets/Environment/HIKARI_EnvironmentAssetStore.h"
#include "Assets/Lighting/HIKARI_IesProfileAsset.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Decal/HIKARI_LocalDecalRecord.h"
#include "Assets/Post/HIKARI_PostAssetStore.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAssetStore.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Render3D/Color/HIKARI_ColorGradingBake.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Scene/Components/Lighting/HIKARI_LightSettings.h"
#include "Scene/Components/Rendering/Decal/HIKARI_DecalComponent.h"
#include "Scene/Environment/HIKARI_CelestialModel.h"
#include "Scene/Environment/HIKARI_EnvironmentRuntime.h"
#include "Scene/Environment/HIKARI_EnvironmentSimulation.h"
#include "Scene/Environment/HIKARI_SceneEnvironmentBinding.h"
#include "Scene/Environment/HIKARI_WeatherModel.h"

// 環境システム（天体・時刻・天気・環境資産・ランタイム状態）と、局所の記録（光源の配光・クッキー、デカール）の振る舞い契約。
namespace {

using namespace HIKARI;
using namespace HIKARI::ENVIRONMENT;

int gFailures = 0;

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAILED line %d: %s\n", line, expression);
        ++gFailures;
    }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)

bool Near(double value, double expected, double tolerance) { return std::abs(value - expected) <= tolerance; }

// 北を 0、東を 90 とする方位角（北偏角 0 のワールドで東 = +X、北 = -Z）。
double Azimuth(const MATH::Vec3& direction) {
    double degrees = std::atan2(direction.x, -direction.z) * 180.0 / 3.14159265358979;
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

void CelestialContracts() {
    const GeoLocation tokyo{};
    // 夏至の東京：南中は約 11:43、高度は 90 - 35.68 + 23.44 ≒ 77.8 度。
    const CelestialState noon = ComputeCelestialState({2026, 6, 21, 11.72}, tokyo);
    CHECK(Near(noon.sunAltitudeDegrees, 77.76, 0.2));
    CHECK(Near(Azimuth(noon.sunDirection), 180.0, 3.0));
    // 朝は東寄り、夕方は西寄り。
    const CelestialState morning = ComputeCelestialState({2026, 6, 21, 8.0}, tokyo);
    const CelestialState evening = ComputeCelestialState({2026, 6, 21, 18.5}, tokyo);
    CHECK(Near(Azimuth(morning.sunDirection), 88.4, 1.0) && morning.sunAltitudeDegrees > 35.0f);
    CHECK(Azimuth(evening.sunDirection) > 285.0 && Near(evening.sunAltitudeDegrees, 4.6, 0.5));
    CHECK(ComputeCelestialState({2026, 6, 21, 23.0}, tokyo).sunAltitudeDegrees < -20.0f);
    CHECK(Near(ComputeCelestialState({2026, 12, 21, 11.7}, tokyo).sunAltitudeDegrees, 30.9, 0.3));
    // 2026-03-03 は満月（皆既月食）、03-19 は新月。
    const CelestialState full = ComputeCelestialState({2026, 3, 3, 20.0}, tokyo);
    const CelestialState fresh = ComputeCelestialState({2026, 3, 19, 12.0}, tokyo);
    CHECK(full.moonIlluminatedFraction > 0.98f && Near(full.moonBrightness, 1.0, 0.05));
    CHECK(fresh.moonIlluminatedFraction < 0.02f && fresh.moonBrightness < 0.01f);
    // 北偏角はワールドでの向きだけを回し、高度は変えない。
    GeoLocation rotated = tokyo;
    rotated.northYawDegrees = 90.0f;
    const CelestialState turned = ComputeCelestialState({2026, 6, 21, 11.72}, rotated);
    CHECK(Near(turned.sunAltitudeDegrees, noon.sunAltitudeDegrees, 1e-3));
    CHECK(!Near(Azimuth(turned.sunDirection), Azimuth(noon.sunDirection), 45.0));
    // 天の北極は真北・高度 = 緯度。
    CHECK(Near(std::asin(full.celestialToWorldZ.y) * 180.0 / 3.14159265358979, tokyo.latitudeDegrees, 0.01));
    CHECK(Near(Azimuth(full.celestialToWorldZ), 0.0, 0.01) || Near(Azimuth(full.celestialToWorldZ), 360.0, 0.01));
    // 不正な入力は既定状態を返す。
    CHECK(!IsValidCalendarTime({2026, 2, 29, 0.0}) && IsValidCalendarTime({2028, 2, 29, 0.0}));
    CHECK(ComputeCelestialState({2026, 13, 1, 0.0}, tokyo).sunAltitudeDegrees == 90.0f);
}

void CalendarContracts() {
    const CalendarTime nextDay = AdvanceCalendarTime({2026, 12, 31, 23.5}, 1.0);
    CHECK(nextDay.year == 2027 && nextDay.month == 1 && nextDay.day == 1 && Near(nextDay.localHours, 0.5, 1e-9));
    const CalendarTime leap = AdvanceCalendarTime({2028, 2, 28, 12.0}, 24.0);
    CHECK(leap.month == 2 && leap.day == 29);
    const CalendarTime back = AdvanceCalendarTime({2026, 3, 1, 1.0}, -2.0);
    CHECK(back.month == 2 && back.day == 28 && Near(back.localHours, 23.0, 1e-9));
    const CalendarTime year = AdvanceCalendarTime({2026, 6, 21, 10.0}, 24.0 * 365.0);
    CHECK(year.year == 2027 && year.month == 6 && year.day == 21);
    CHECK(AdvanceCalendarTime({2026, 6, 21, 10.0}, NAN).localHours == 10.0);
}

void WeatherContracts() {
    WeatherParameters clear{};
    WeatherParameters storm{};
    storm.cloudCoverage = 0.95f;
    storm.cloudType = 1.0f;
    storm.precipitation = PrecipitationType::Rain;
    storm.precipitationIntensity = 1.0f;
    storm.windFromDegrees = 10.0f;
    clear.windFromDegrees = 350.0f;
    // 中間値・風向の最短補間（350 -> 10 は 0 を通る）。
    const WeatherParameters half = BlendWeather(clear, storm, 0.5f);
    CHECK(Near(half.cloudCoverage, (clear.cloudCoverage + 0.95f) * 0.5f, 1e-5));
    CHECK(Near(half.windFromDegrees, 0.0, 1e-3) || Near(half.windFromDegrees, 360.0, 1e-3));
    CHECK(BlendWeather(clear, storm, 0.0f) == clear && BlendWeather(clear, storm, 1.0f).cloudType == 1.0f);
    // 雨から雪へは強度が 0 を通ってから種類が変わる。
    WeatherParameters snow = storm;
    snow.precipitation = PrecipitationType::Snow;
    snow.precipitationIntensity = 0.8f;
    const WeatherParameters early = BlendWeather(storm, snow, 0.25f);
    const WeatherParameters late = BlendWeather(storm, snow, 0.75f);
    CHECK(early.precipitation == PrecipitationType::Rain && Near(early.precipitationIntensity, 0.5, 1e-5));
    CHECK(late.precipitation == PrecipitationType::Snow && Near(late.precipitationIntensity, 0.4, 1e-5));
    CHECK(BlendWeather(storm, snow, 0.5f).precipitationIntensity == 0.0f);
    CHECK(BlendWeather(clear, storm, NAN) == BlendWeather(clear, storm, 1.0f));

    // 地表は時間をかけて濡れ、晴れると乾く。積雪は雨で早く融ける。
    SurfaceWeatherState surface{};
    for (int second = 0; second < 30; ++second) {
        surface = AdvanceSurfaceWeather(surface, storm, 1.0f);
    }
    CHECK(surface.wetness == 1.0f && surface.puddles > 0.0f && surface.snowCover == 0.0f);
    const SurfaceWeatherState dried = AdvanceSurfaceWeather(surface, clear, 60.0f);
    CHECK(dried.wetness < surface.wetness && dried.wetness > 0.0f);
    CHECK(AdvanceSurfaceWeather(surface, clear, -1.0f) == surface);
    SurfaceWeatherState snowy{0.0f, 0.0f, 0.5f};
    CHECK(AdvanceSurfaceWeather(snowy, storm, 10.0f).snowCover < AdvanceSurfaceWeather(snowy, clear, 10.0f).snowCover);
    CHECK(AdvanceSurfaceWeather({}, snow, 100.0f).snowCover > 0.2f);

    // JSON は正規化した値で往復し、不正値は既定値か範囲へ戻す。
    WeatherParameters loaded{};
    ReadWeatherParameters(WriteWeatherParameters(storm), loaded);
    CHECK(loaded == NormalizeWeatherParameters(storm));
    WeatherParameters broken{};
    ReadWeatherParameters({{"cloudCoverage", 4.0}, {"precipitation", "Hail"}, {"fogDensity", -1.0}}, broken);
    CHECK(broken.cloudCoverage == 1.0f && broken.precipitation == PrecipitationType::None && broken.fogDensity == 0.0f);
    WeatherParameters dry{};
    ReadWeatherParameters({{"precipitation", "None"}, {"precipitationIntensity", 0.7}}, dry);
    CHECK(dry.precipitationIntensity == 0.0f);
}

void SimulationContracts() {
    WeatherParameters clear{};
    WeatherParameters storm{};
    storm.cloudCoverage = 1.0f;
    storm.precipitation = PrecipitationType::Rain;
    storm.precipitationIntensity = 1.0f;
    storm.lightningPerMinute = 30.0f;

    // 時刻は timeScale 倍、天気は実時間で進む。
    EnvironmentSimulation simulation;
    simulation.Reset({2026, 6, 21, 23.5}, GeoLocation{}, clear, 1u, 3600.0);
    simulation.RequestWeather(storm, 2u, 10.0f);
    simulation.Advance(1.0f);
    EnvironmentFrameState frame = simulation.GetFrameState();
    CHECK(frame.time.day == 22 && Near(frame.time.localHours, 0.5, 1e-6));
    CHECK(frame.weatherTargetId == 2u && Near(frame.weatherTransitionProgress, 0.1, 1e-6));
    CHECK(frame.weather.cloudCoverage > clear.cloudCoverage && frame.weather.cloudCoverage < 1.0f);
    // 遷移中の再要求は現在の補間値から始まり、値が跳ばない。
    const float before = frame.weather.cloudCoverage;
    simulation.RequestWeather(clear, 3u, 5.0f);
    CHECK(Near(simulation.GetFrameState().weather.cloudCoverage, before, 1e-6));
    for (int step = 0; step < 10; ++step) {
        simulation.Advance(1.0f);
    }
    CHECK(simulation.GetFrameState().weather == NormalizeWeatherParameters(clear));
    simulation.SetTimeScale(-5.0);
    CHECK(simulation.GetSnapshot().timeScale == 0.0);

    // 雲は風下へ流れる。西風（吹いてくる方位 270 度）なら東（+X）へ、移動量は風速 × 実時間で時刻の倍率に依らない。
    WeatherParameters westWind{};
    westWind.windFromDegrees = 270.0f;
    westWind.windSpeed = 10.0f;
    EnvironmentSimulation wind;
    wind.Reset({2026, 6, 21, 12.0}, GeoLocation{}, westWind, 4u, 3600.0);
    for (int step = 0; step < 20; ++step) {
        wind.Advance(0.5f);
    }
    const EnvironmentFrameState windFrame = wind.GetFrameState();
    CHECK(Near(windFrame.windDisplacementX, 100.0, 1e-3) && Near(windFrame.windDisplacementZ, 0.0, 1e-3));
    CHECK(Near(windFrame.cloudSeconds, 10.0, 1e-9) && Near(windFrame.windVelocity.x, 10.0, 1e-4));

    // 同じ入力列なら落雷も同じ。スナップショットから同じ続きを再現できる。
    EnvironmentSimulation a;
    EnvironmentSimulation b;
    a.Reset({2026, 6, 21, 12.0}, GeoLocation{}, storm, 2u, 1.0);
    b.Reset({2026, 6, 21, 12.0}, GeoLocation{}, storm, 2u, 1.0);
    int flashes = 0;
    for (int step = 0; step < 600; ++step) {
        a.Advance(0.1f);
        b.Advance(0.1f);
        const EnvironmentFrameState strike = a.GetFrameState();
        flashes += strike.lightningFlash == 1.0f ? 1 : 0;
        // 落雷は視点から 2〜12 km、経過時間は閃光の減衰から戻る。
        CHECK(strike.lightningFlash == 0.0f ||
              (strike.lightningDistance >= 2000.0f && strike.lightningDistance <= 12000.0f &&
               strike.lightningAge >= 0.0f && strike.lightningAge <= 0.25f));
        CHECK(a.GetSnapshot() == b.GetSnapshot());
    }
    CHECK(flashes > 10 && flashes < 60);
    EnvironmentSimulation restored;
    CHECK(restored.Restore(a.GetSnapshot()));
    a.Advance(0.5f);
    restored.Advance(0.5f);
    CHECK(a.GetSnapshot() == restored.GetSnapshot());
    EnvironmentSnapshot invalid = a.GetSnapshot();
    invalid.weather.elapsedSeconds = invalid.weather.durationSeconds + 1.0f;
    CHECK(!restored.Restore(invalid) && restored.GetSnapshot() == a.GetSnapshot());
}

void AssetContracts() {
    // 保存した形をそのまま読み戻せる。範囲外の値は正規化され、形式の版の無い資産は拒否する。
    EnvironmentProfileAsset profile{};
    profile.name = "Dusk";
    profile.profile.sky.source = SkySource::Hdri;
    profile.profile.sky.asset = "0123456789abcdef0123456789abcdef";
    profile.profile.sun.mode = SunMode::Fixed;
    profile.profile.sun.fixedElevationDegrees = 4.0f;
    profile.profile.atmosphere.belowHorizon = BelowHorizonMode::Ground;
    profile.profile.fog.density = 9.0f;
    profile.profile.exposure.mode = RENDER3D::COLOR::ExposureMode::Manual;
    profile.profile.shadows.contactShadows = true;
    profile.profile.shadows.contactShadowThickness = 9.0f;
    nlohmann::json written = WriteEnvironmentProfileAsset(profile);
    EnvironmentProfileAsset read{};
    std::string error;
    CHECK(ReadEnvironmentProfileAsset(written, read, error));
    CHECK(read.profile.sky.source == SkySource::Hdri && read.profile.sun.mode == SunMode::Fixed &&
          read.profile.atmosphere.belowHorizon == BelowHorizonMode::Ground);
    CHECK(read.profile.fog.density == 1.0f && WriteEnvironmentProfileAsset(read) == WriteEnvironmentProfileAsset(read));
    CHECK(read.profile.shadows.contactShadows && read.profile.shadows.contactShadowThickness == 2.0f);
    CHECK(CollectEnvironmentProfileDependencies(read).size() == 1u);
    written.erase("formatVersion");
    CHECK(!ReadEnvironmentProfileAsset(written, read, error) && !error.empty());

    WeatherPresetAsset weather{};
    weather.weather.precipitation = PrecipitationType::Snow;
    weather.weather.precipitationIntensity = 0.5f;
    WeatherPresetAsset weatherRead{};
    CHECK(ReadWeatherPresetAsset(WriteWeatherPresetAsset(weather), weatherRead, error));
    CHECK(weatherRead.weather == weather.weather);

    // 描画の様式: 着色の範囲外は丸め、後処理の層は内蔵 ID か GUID だけ、内蔵の層は依存に数えない。
    RENDERSTYLE::RenderStyleAsset style{};
    style.shading.model = RENDERSTYLE::RenderStyleShadingModel::Cel;
    style.shading.bands = 9u;
    style.post = {{"engine.post.profile.manga", true}, {"0123456789abcdef0123456789abcdef", false}};
    nlohmann::json styleJson = RENDERSTYLE::WriteRenderStyleAsset(style);
    RENDERSTYLE::RenderStyleAsset styleRead{};
    CHECK(RENDERSTYLE::ReadRenderStyleAsset(styleJson, styleRead, error));
    CHECK(styleRead.shading.model == RENDERSTYLE::RenderStyleShadingModel::Cel && styleRead.shading.bands == 4u &&
          styleRead.post == style.post && RENDERSTYLE::CollectRenderStyleDependencies(styleRead).size() == 1u);
    styleJson["post"][0]["profile"] = "manga";
    CHECK(!RENDERSTYLE::ReadRenderStyleAsset(styleJson, styleRead, error) && !error.empty());

    // シーンの参照: 不正な日付は初期値へ、空のプロファイルは既定へ。
    SceneEnvironmentBinding binding{};
    binding.weatherId = "engine.weather.rain";
    binding.timeScale = 60.0;
    nlohmann::json bindingJson = WriteSceneEnvironmentBinding(binding);
    SceneEnvironmentBinding bindingRead{};
    ReadSceneEnvironmentBinding(bindingJson, bindingRead);
    CHECK(bindingRead == binding);
    bindingJson["profile"] = "";
    bindingJson["renderStyle"] = "";
    bindingJson["time"]["month"] = 2;
    bindingJson["time"]["day"] = 30;
    ReadSceneEnvironmentBinding(bindingJson, bindingRead);
    CHECK(bindingRead.profileId == SceneEnvironmentBinding{}.profileId && bindingRead.time == CalendarTime{} &&
          bindingRead.renderStyleId == RENDERSTYLE::kDefaultRenderStyleId);

    // エンジン内蔵の内容はすべて読める。
    CHECK(CONTENT::GetEngineContentContext().Open(HIKARI_CONTRACT_ENGINE_CONTENT_ROOT));
    EnvironmentAssetStore store;
    store.Configure(nullptr);
    for (const BuiltinEnvironmentContent& content : GetBuiltinEnvironmentProfiles()) {
        CHECK(store.LoadProfile(content.id, error) != nullptr);
    }
    for (const BuiltinEnvironmentContent& content : GetBuiltinWeatherPresets()) {
        CHECK(store.LoadWeather(content.id, error) != nullptr);
    }
    // 内蔵の描画の様式と、その後処理の層が指すプロファイル・効果も読める。
    RENDERSTYLE::RenderStyleAssetStore styles;
    styles.Configure(nullptr);
    POST::PostAssetStore posts;
    posts.Configure(nullptr);
    for (const RENDERSTYLE::BuiltinRenderStyleContent& content : RENDERSTYLE::GetBuiltinRenderStyles()) {
        const auto builtinStyle = styles.Load(content.id, error);
        CHECK(builtinStyle != nullptr);
        if (!builtinStyle) {
            continue;
        }
        for (const RENDERSTYLE::RenderStylePostLayer& layer : builtinStyle->post) {
            const auto postProfile = posts.LoadProfile(layer.profileId, error);
            CHECK(postProfile != nullptr && posts.LoadEffect(postProfile->effectId, error) != nullptr);
        }
    }
}

// 色調整: .cube の読み込みと拒否、恒等の判定、焼いた LUT の暗部・明部の倍率。
void ColorGradingContracts() {
    namespace LUT = ASSETS::COLOR;
    const std::filesystem::path root = std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) / "ColorGradingContracts";
    std::filesystem::create_directories(root);
    const auto load = [&](const char* name, const std::string& text, LUT::ColorLutAsset& lut, std::string& error) {
        const std::filesystem::path path = root / name;
        std::ofstream(path, std::ios::binary) << text;
        return LUT::LoadColorLutAsset(path, lut, error);
    };
    // 赤が最も速い順の 2^3 の恒等表。定義域 [0, 2] なので 1 は表の中央。
    const std::string identity = "# comment\nTITLE \"Identity\"\nLUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n"
                                 "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    LUT::ColorLutAsset lut;
    std::string error;
    CHECK(load("Identity.cube", identity, lut, error) && lut.size == 2u && lut.title == "Identity");
    const MATH::Vec3 sampled = LUT::SampleColorLut(lut, {1.0f, 0.5f, 2.0f});
    CHECK(Near(sampled.x, 0.5, 1e-6) && Near(sampled.y, 0.25, 1e-6) && Near(sampled.z, 1.0, 1e-6));
    LUT::ColorLutAsset rejected;
    CHECK(!load("Short.cube", "LUT_3D_SIZE 2\n0 0 0\n", rejected, error) && !error.empty());
    CHECK(!load("OneDimensional.cube", "LUT_1D_SIZE 2\n0 0 0\n1 1 1\n", rejected, error));
    CHECK(!load("Unknown.cube", "LUT_IN_VIDEO_RANGE\nLUT_3D_SIZE 2\n", rejected, error));

    ColorGradingSettings grading{};
    CHECK(!RENDER3D::COLOR::IsColorGradingActive(grading, nullptr));
    CHECK(RENDER3D::COLOR::IsColorGradingActive(grading, &lut));
    grading.lutIntensity = 0.0f;
    CHECK(!RENDER3D::COLOR::IsColorGradingActive(grading, &lut));
    grading.shadowsTint = {2.0f, 1.0f, 1.0f};
    CHECK(RENDER3D::COLOR::IsColorGradingActive(grading, nullptr));
    std::vector<uint16_t> texels;
    RENDER3D::COLOR::BakeColorGradingLut(grading, nullptr, 2.2f, texels);
    constexpr size_t size = RENDER3D::COLOR::kColorGradingLutSize;
    CHECK(texels.size() == size * size * size * 4u);
    const auto texel = [&](size_t r, size_t g, size_t b, size_t channel) {
        return DirectX::PackedVector::XMConvertHalfToFloat(texels[((b * size + g) * size + r) * 4u + channel]);
    };
    // 暗い灰色の赤だけが暗部の重みの分だけ 2^(1/2.2) 倍へ寄る。黒と白はそのまま（白は明部の倍率 1）。
    const float gray = 4.0f / static_cast<float>(size - 1u);
    const float shadowT = gray / 0.4f;
    const float shadowWeight = 1.0f - shadowT * shadowT * (3.0f - 2.0f * shadowT);
    const float redGain = shadowWeight * std::pow(2.0f, 1.0f / 2.2f) + (1.0f - shadowWeight);
    CHECK(Near(texel(4, 4, 4, 0), gray * redGain, 2e-3) && Near(texel(4, 4, 4, 1), gray, 2e-3));
    CHECK(texel(0, 0, 0, 0) == 0.0f && Near(texel(size - 1, size - 1, size - 1, 0), 1.0, 1e-3));

    // プロファイルの LUT は資産の依存になる。
    EnvironmentProfileAsset profile{};
    profile.profile.colorGrading.lut = "0123456789abcdef0123456789abcdef";
    CHECK(CollectEnvironmentProfileDependencies(profile).size() == 1u);
    std::filesystem::remove_all(root);
}

// 局所光の配光: IES（Type C）の読み込みと拒否、対称性の展開、全光束を保つ倍率。作者値と GPU 記録への詰め方。
void LightProfileContracts() {
    namespace IES = ASSETS::LIGHTING;
    const std::filesystem::path root = std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) / "LightProfileContracts";
    std::filesystem::create_directories(root);
    const auto load = [&](const char* name, const std::string& text, IES::IesProfileAsset& profile,
                          std::string& error) {
        const std::filesystem::path path = root / name;
        std::ofstream(path, std::ios::binary) << text;
        return IES::LoadIesProfileAsset(path, profile, error);
    };
    // 回転対称の下向き配光: 0° で 1000 cd、45° で 500 cd、90° より上は 0。
    const std::string header = "IESNA:LM-63-2002\n[TEST] contract\nTILT=NONE\n";
    IES::IesProfileAsset profile;
    std::string error;
    CHECK(load("Downlight.ies", header + "1 1000 1 3 1 1 2 0 0 0\n1 1 10\n0 45 90\n0\n1000 500 0\n", profile, error) &&
          profile.maximumCandela == 1000.0f && profile.verticalAngles.size() == 3u);
    const IES::IesProfileTable table = IES::SampleIesProfile(profile);
    constexpr size_t vertical = IES::kIesProfileVerticalSamples;
    CHECK(table.values.size() == vertical * IES::kIesProfileHorizontalSamples);
    const auto at = [&](size_t h, size_t v) { return table.values[h * vertical + v]; };
    // 60°（v = 21）は 45..90° の補間で 1/3、上半球は 0。回転対称なのでどの水平角も同じ。
    CHECK(at(0, 0) == 1.0f && Near(at(0, 21), 1.0 / 3.0, 1e-4) && Near(at(17, 21), at(0, 21), 1e-6));
    CHECK(at(0, vertical - 1u) == 0.0f);
    // 全球の平均光度との比（解析値 5.504）。描画はこの倍率で光度を上げ、全光束を作者値に揃える。
    CHECK(Near(table.peakToMean, 5.504, 0.01));

    // 四象限対称（0..90°）は 360° へ折り返して展開する。水平角 92.9° は 87.1° の値。
    CHECK(load("Quadrant.ies", header + "1 1000 2 2 2 1 2 0 0 0 1 1 10 0 90 0 90 500 400 500 200\n", profile, error) &&
          profile.maximumCandela == 1000.0f);
    const IES::IesProfileTable quadrant = IES::SampleIesProfile(profile);
    const auto quadrantAt = [&](size_t h, size_t v) { return quadrant.values[h * vertical + v]; };
    CHECK(Near(quadrantAt(0, 21), 13.0 / 15.0, 1e-4) && Near(quadrantAt(8, 21), 0.6086, 2e-3));
    CHECK(Near(quadrantAt(5, 21), quadrantAt(26, 21), 1e-6) && Near(quadrantAt(31, 21), quadrantAt(0, 21), 1e-6));

    IES::IesProfileAsset rejected;
    CHECK(!load("NoTilt.ies", "1 1000 1 1 1 1 2 0 0 0 1 1 10 0 0 100\n", rejected, error) && !error.empty());
    CHECK(!load("TypeB.ies", header + "1 1000 1 1 1 2 2 0 0 0 1 1 10 0 0 100\n", rejected, error));
    CHECK(!load("Short.ies", header + "1 1000 1 3 1 1 2 0 0 0 1 1 10 0 45 90 0 1000 500\n", rejected, error));
    CHECK(!load("TiltFile.ies", "IESNA:LM-63-2002\nTILT=lamp.tilt\n", rejected, error));

    // 作者値は GUID を保って往復し、GPU 記録は水平の基準と記述子番号 + 1 を詰める（点光源も向きを持つ）。
    LightSettings settings{};
    settings.type = RENDER3D::LIGHTING::SceneLightType::Spot;
    settings.iesProfile = "0123456789abcdef0123456789abcdef";
    settings.cookie = "fedcba9876543210fedcba9876543210";
    CHECK(ReadLightSettings(WriteLightSettings(settings)) == settings);
    // 相対の明るさは基準照度に比例し、単位を切り替えても今の光度を保つ。
    settings.intensityUnit = LightIntensityUnit::SceneRelative;
    settings.sceneRelativeIntensity = 0.5f;
    CHECK(ReadLightSettings(WriteLightSettings(settings)) == settings);
    SetSceneKeyIlluminance(1000.0f);
    const LightSettings physical = ConvertLightIntensityUnit(settings, LightIntensityUnit::Lumens);
    CHECK(Near(ResolveLuminousIntensity(settings), 500.0, 1e-3) &&
          Near(ResolveLuminousIntensity(physical), 500.0, 1e-2));
    SetSceneKeyIlluminance(2000.0f);
    CHECK(Near(ResolveLuminousIntensity(settings), 1000.0, 1e-3) &&
          Near(ResolveLuminousIntensity(physical), 500.0, 1e-2));
    CHECK(Near(ConvertLightIntensityUnit(physical, LightIntensityUnit::SceneRelative).sceneRelativeIntensity, 0.25,
               1e-5));
    RENDER3D::LIGHTING::SceneLightProvider light{};
    light.type = RENDER3D::LIGHTING::SceneLightType::Spot;
    light.direction = {0.0f, -1.0f, 0.0f};
    light.tangent = {1.0f, 0.0f, 0.0f};
    light.iesProfileTexture = 5u;
    light.cookieTexture = 7u;
    RENDER3D::LIGHTING::LocalLightGpuData gpu = RENDER3D::LIGHTING::BuildLocalLightGpuData(light);
    CHECK(gpu.shapeTangent.w == static_cast<float>(5u + 7u * RENDER3D::LIGHTING::kLocalLightTextureIndexStride) &&
          gpu.shapeTangent.x == 1.0f && gpu.directionInnerCos.y == -1.0f);
    light.type = RENDER3D::LIGHTING::SceneLightType::Point;
    light.cookieTexture = 0u;
    gpu = RENDER3D::LIGHTING::BuildLocalLightGpuData(light);
    CHECK(gpu.shapeTangent.w == 5.0f && gpu.directionInnerCos.y == -1.0f);
    std::filesystem::remove_all(root);
}

// デカール: 作者値の正規化と往復、GPU 記録の箱の空間、光源の後ろへの並びとクラスタの詰め方。
void DecalContracts() {
    namespace DECAL = RENDER3D::DECAL;
    namespace LIGHTING = RENDER3D::LIGHTING;
    DECAL::DecalSettings settings{};
    settings.baseColorTexture = "0123456789abcdef0123456789abcdef";
    settings.size = {1.0f, 0.5f, 0.25f};
    settings.color = {1.0f, 0.5f, 0.0f};
    CHECK(ReadDecalSettings(WriteDecalSettings(settings)) == settings);
    nlohmann::json loose = WriteDecalSettings(settings);
    loose["opacity"] = 3.0;
    loose["size"] = {0.0, 1.0, 1.0};
    const DECAL::DecalSettings clamped = ReadDecalSettings(loose);
    CHECK(clamped.opacity == 1.0f && clamped.size.x == 0.01f);

    // 物体（位置 (1, 2, 3)、拡縮 2）の箱は半辺 (1, 0.5, 0.25)。角は箱の空間の (0.5, 0.5, 0.5)。
    DECAL::SceneDecalProvider decal{
        "decal_1", MATH::Mat4::Translate({1.0f, 2.0f, 3.0f}) * MATH::Mat4::Scale({2.0f, 2.0f, 2.0f}), settings};
    decal.baseColorTextureIndex = 9u;
    CHECK(DECAL::IsDecalUploadable(decal));
    const LIGHTING::LocalDecalGpuData gpu = DECAL::BuildLocalDecalGpuData(decal);
    const auto toBox = [&](const MATH::Vec3& p) {
        const auto row = [&](const MATH::Vec4& r) { return r.x * p.x + r.y * p.y + r.z * p.z + r.w; };
        return MATH::Vec3{row(gpu.worldToDecal[0]), row(gpu.worldToDecal[1]), row(gpu.worldToDecal[2])};
    };
    const MATH::Vec3 corner = toBox({2.0f, 2.5f, 3.25f});
    const MATH::Vec3 center = toBox({1.0f, 2.0f, 3.0f});
    CHECK(Near(corner.x, 0.5, 1e-5) && Near(corner.y, 0.5, 1e-5) && Near(corner.z, 0.5, 1e-5));
    CHECK(Near(center.x, 0.0, 1e-6) && Near(center.y, 0.0, 1e-6) && Near(center.z, 0.0, 1e-6));
    CHECK((gpu.kind & 0xffu) == static_cast<uint32_t>(LIGHTING::LocalLightType::Decal) &&
          ((gpu.kind >> 8u) & 0xffu) == 255u && ((gpu.kind >> 16u) & 0xffu) == 128u && (gpu.kind >> 24u) == 0u);
    // 法線テクスチャが無ければ法線の重みは 0。何も乗せないデカールは送らない。
    CHECK(gpu.textures == 9.0f && gpu.normalWeight == 0.0f && gpu.colorWeight == 1.0f);
    DECAL::SceneDecalProvider inert = decal;
    inert.settings.colorWeight = 0.0f;
    CHECK(!DECAL::IsDecalUploadable(inert));

    // 光源とデカールが同じクラスタに入ると、index 列は光源の後ろにデカール、header は 1 | 1 << 16。
    LIGHTING::SceneLightingSet lighting{};
    LIGHTING::SceneLightProvider light{};
    light.id = "light_1";
    light.enabled = true;
    light.position = {0.0f, 0.0f, -5.0f};
    light.range = 2.0f;
    light.intensity = 10.0f;
    lighting.lights.push_back(light);
    decal.world = MATH::Mat4::Translate({0.0f, 0.0f, -5.0f});
    lighting.decals.push_back(decal);
    Camera3D camera{};
    camera.SetPerspective(1.0f, 1.0f, 0.1f, 100.0f);
    camera.SetLookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    std::vector<LIGHTING::LocalLightGpuData> records(4u);
    std::vector<LIGHTING::LocalLightClusterHeader> headers(LIGHTING::kMaximumLocalLightClusterCount);
    std::vector<uint32_t> indices(4096u);
    const LIGHTING::LocalLightListView view = LIGHTING::BuildLocalLightList(
        lighting, camera, 256u, 256u,
        {records.data(), static_cast<uint32_t>(records.size()), headers.data(), static_cast<uint32_t>(headers.size()),
         indices.data(), static_cast<uint32_t>(indices.size())});
    CHECK(view.lightCount == 1u && view.decalCount == 1u && (records[1].kind & 0xffu) == 4u);
    bool shared = false;
    for (uint32_t cluster = 0u; cluster < view.clusterCount; ++cluster) {
        const LIGHTING::LocalLightClusterHeader& header = headers[cluster];
        if (header.count == (1u | 1u << 16u)) {
            shared = indices[header.offset] == 0u && indices[header.offset + 1u] == 1u;
            break;
        }
    }
    CHECK(shared);
}

// 実際に光錐内の点を投影し、列の絞り込みが照明を失わず名額とデカールも変えないことを確かめる。
void SpotClusterContracts() {
    using namespace RENDER3D::LIGHTING;
    constexpr uint32_t width = 853u, height = 479u;
    std::vector<LocalLightGpuData> records(128u);
    std::vector<LocalLightClusterHeader> headers(kMaximumLocalLightClusterCount);
    std::vector<uint32_t> indices(kMaximumLocalLightClusterIndexCount);
    const auto build = [&](const SceneLightingSet& lights, const Camera3D& camera, uint32_t capacity) {
        return BuildLocalLightList(lights, camera, width, height,
                                   {records.data(), uint32_t(records.size()), headers.data(), uint32_t(headers.size()),
                                    indices.data(), capacity});
    };
    const auto contains = [](const auto& list, const auto& links, uint32_t cluster, uint32_t record) {
        const auto& header = list[cluster];
        for (uint32_t i = 0u; i < (header.count & 0xffffu); ++i)
            if (links[header.offset + i] == record)
                return true;
        return false;
    };
    uint32_t checked = 0u;
    for (uint32_t variant = 0u; variant < 4u; ++variant) {
        Camera3D camera;
        const MATH::Vec3 shift = variant >= 2u ? MATH::Vec3{1000, -800, 600} : MATH::Vec3{};
        camera.SetLookAt(shift, shift + MATH::Vec3{0, 0, -1});
        if ((variant & 1u) != 0u)
            camera.SetOrthographic(12.0f, float(width) / height, 0.1f, 100.0f);
        else
            camera.SetPerspective(1.0f, float(width) / height, 0.1f, 100.0f);
        SceneLightingSet lights;
        for (uint32_t i = 0u; i < 5u; ++i) {
            SceneLightProvider light;
            light.enabled = true;
            light.type = SceneLightType::Spot;
            light.position = shift + MATH::Vec3{float(i) - 2.0f, 1.0f, i == 4u ? 0.05f : -4.0f};
            light.direction = MATH::Normalize({0.35f * (float(i) - 2.0f), -0.2f, -1.0f});
            light.intensity = 10;
            light.range = 14;
            light.outerConeAngleDegrees = i == 3u ? 89.0f : 8.0f + 5.0f * float(i);
            lights.lights.push_back(light);
        }
        RENDER3D::DECAL::SceneDecalProvider decal;
        decal.world = MATH::Mat4::Translate(shift + MATH::Vec3{0, 0, -6});
        lights.decals.push_back(decal);
        SceneLightingSet spheres = lights;
        for (auto& light : spheres.lights)
            light.type = SceneLightType::Point;
        const auto baseline = build(spheres, camera, uint32_t(indices.size()));
        const auto oldHeaders = headers;
        const auto oldIndices = indices;
        const auto refined = build(lights, camera, uint32_t(indices.size()));
        CHECK(refined.lightCount == baseline.lightCount && refined.overflowCount == baseline.overflowCount);
        CHECK(refined.clusterIndexCount < baseline.clusterIndexCount);
        for (uint32_t cluster = 0u; cluster < refined.clusterCount; ++cluster) {
            const auto& current = headers[cluster];
            const auto& previous = oldHeaders[cluster];
            CHECK((current.count >> 16u) == (previous.count >> 16u));
            for (uint32_t i = 0u; i < (current.count >> 16u); ++i)
                CHECK(indices[current.offset + (current.count & 0xffffu) + i] ==
                      oldIndices[previous.offset + (previous.count & 0xffffu) + i]);
        }
        for (uint32_t lightIndex = 0u; lightIndex < lights.lights.size(); ++lightIndex) {
            const auto& light = lights.lights[lightIndex];
            const auto axis = light.direction;
            const auto tangent = MATH::Normalize(MATH::Cross(axis, {0, 1, 0}));
            const auto bitangent = MATH::Cross(axis, tangent);
            for (uint32_t ring = 0u; ring < 5u; ++ring)
                for (uint32_t azimuth = 0u; azimuth < 48u; ++azimuth)
                    for (uint32_t depth = 1u; depth <= 8u; ++depth) {
                        const float angle = light.outerConeAngleDegrees * 0.01745329252f * float(ring) * 0.24999f;
                        const float phi = float(azimuth) * 6.28318530718f / 48.0f;
                        const auto direction = axis * std::cos(angle) +
                                               (tangent * std::cos(phi) + bitangent * std::sin(phi)) * std::sin(angle);
                        const auto point = light.position + direction * (light.range * float(depth) * 0.12499f);
                        const auto clip = camera.GetViewProj().TransformPoint({point.x, point.y, point.z, 1});
                        if (clip.w <= 0 || std::abs(clip.x) >= clip.w || std::abs(clip.y) >= clip.w || clip.z < 0 ||
                            clip.z > clip.w)
                            continue;
                        const uint32_t x = uint32_t((clip.x / clip.w * 0.5f + 0.5f) * width) / refined.tileSizePixels;
                        const uint32_t y = uint32_t((0.5f - clip.y / clip.w * 0.5f) * height) / refined.tileSizePixels;
                        // 同じ列の全深度を確認する。霧の等角度標本がクラスタの深度境界を跨いでも残る。
                        for (uint32_t z = 0u; z < refined.clusterCountZ; ++z) {
                            const uint32_t cluster = (z * refined.clusterCountY + y) * refined.clusterCountX + x;
                            if (contains(oldHeaders, oldIndices, cluster, lightIndex)) {
                                CHECK(contains(headers, indices, cluster, lightIndex));
                                ++checked;
                            }
                        }
                    }
        }
        // 元の上限で選ばれなかった灯りは、空きができても繰り上げない。index 容量の境界も同じ。
        lights.lights.assign(65u, lights.lights[0]);
        lights.lights.back().type = SceneLightType::Point;
        spheres = lights;
        for (auto& light : spheres.lights)
            light.type = SceneLightType::Point;
        for (const uint32_t capacity : {uint32_t(indices.size()), 37u}) {
            const auto limitedBase = build(spheres, camera, capacity);
            const auto limitedHeaders = headers;
            const auto limitedIndices = indices;
            const auto limited = build(lights, camera, capacity);
            CHECK(limited.overflowCount == limitedBase.overflowCount);
            for (uint32_t cluster = 0u; cluster < limited.clusterCount; ++cluster) {
                const auto& header = headers[cluster];
                uint32_t last = 0u;
                for (uint32_t i = 0u; i < (header.count & 0xffffu); ++i) {
                    const uint32_t record = indices[header.offset + i];
                    CHECK(contains(limitedHeaders, limitedIndices, cluster, record));
                    CHECK(i == 0u || record > last);
                    last = record;
                }
            }
        }
    }
    CHECK(checked > 10000u);
    std::printf("Spot cluster retained illuminated samples across depth slices: %u\n", checked);
}

void RuntimeContracts() {
    EnvironmentAssetStore store;
    store.Configure(nullptr);
    EnvironmentRuntime runtime;
    runtime.Configure(&store);
    SceneEnvironmentBinding binding{};
    binding.time = {2026, 6, 21, 12.0};
    runtime.Reset(binding);
    CHECK(runtime.GetError().empty() && runtime.GetWeatherId() == kDefaultWeatherId);
    // 昼は太陽、深夜は月が影の主光源。
    SceneEnvironment noon = runtime.ResolveFrame();
    CHECK(!noon.frame.moonIsPrimary && noon.frame.sun.illuminance > 100000.0f && noon.frame.sun.toLight.y > 0.9f);
    binding.time = {2026, 6, 29, 0.5};
    runtime.Reset(binding);
    const SceneEnvironment night = runtime.ResolveFrame();
    CHECK(night.frame.sun.horizonVisibility == 0.0f);
    CHECK(night.frame.moonIsPrimary || ATMOSPHERE::BuildAtmosphereSolarState(night).sunRadiance == 0.0f);
    CHECK(night.frame.moonIsPrimary == (night.frame.moon.illuminance * night.frame.moon.horizonVisibility > 0.0f));

    // 満月の夜は月が主光源で、大気を通った月光が地表へ届く。
    binding.time = {2026, 6, 30, 23.5};
    runtime.Reset(binding);
    const SceneEnvironment fullMoon = runtime.ResolveFrame();
    const auto moonlight = ATMOSPHERE::BuildAtmosphereSolarState(fullMoon);
    CHECK(fullMoon.frame.moonIsPrimary && fullMoon.frame.moon.illuminance > 0.2f);
    CHECK(moonlight.valid && moonlight.sunRadiance > 0.05f);

    // 天気は遷移時間をかけて重なる。知らない天気は拒否し、今の天気を保つ。
    std::string error;
    CHECK(runtime.RequestWeather("engine.weather.fog", 0.0f, error));
    runtime.Advance(0.0f);
    const SceneEnvironment foggy = runtime.ResolveFrame();
    CHECK(foggy.fog.enabled && foggy.fog.density > night.fog.density);
    CHECK(!runtime.RequestWeather("engine.weather.unknown", 1.0f, error) &&
          runtime.GetWeatherId() == "engine.weather.fog");

    // 固定の太陽は時刻に依らない。方位 90 は東（+X）。
    EnvironmentProfile fixed{};
    fixed.sun.mode = SunMode::Fixed;
    fixed.sun.fixedAzimuthDegrees = 90.0f;
    fixed.sun.fixedElevationDegrees = 30.0f;
    const EnvironmentLightingFrame frame = BuildLightingFrame(fixed, EnvironmentFrameState{}, GeoLocation{});
    CHECK(Near(frame.sun.toLight.x, std::cos(30.0 * 3.14159265358979 / 180.0), 1e-4) &&
          Near(frame.sun.toLight.y, 0.5, 1e-4) && !frame.moonIsPrimary);
    // 日没直後（-1.5°）は地表に直射が届かないが、高さ 9km の雲にはまだ日が当たる（薄明と高い雲の夕焼け）。
    fixed.sun.fixedElevationDegrees = -1.5f;
    SceneEnvironment dusk{};
    static_cast<EnvironmentProfile&>(dusk) = fixed;
    dusk.frame = BuildLightingFrame(fixed, EnvironmentFrameState{}, GeoLocation{});
    CHECK(dusk.frame.sun.horizonVisibility == 0.0f && ATMOSPHERE::BuildAtmosphereSolarState(dusk).sunRadiance == 0.0f);
    CHECK(ATMOSPHERE::BuildAtmosphereSolarState(dusk, 9000.0f).sunRadiance > 1000.0f);

    // 存档から戻すと同じ状態になる。
    const EnvironmentSnapshot snapshot = runtime.Simulation().GetSnapshot();
    runtime.Reset(binding);
    CHECK(runtime.Restore(snapshot, "engine.weather.fog", "engine.style.sketch") &&
          runtime.Simulation().GetSnapshot() == snapshot && runtime.GetRenderStyleId() == "engine.style.sketch");
    CHECK(!runtime.Restore(snapshot, "engine.weather.rain", "engine.style.sketch"));
    CHECK(!runtime.Restore(snapshot, "engine.weather.fog", ""));
}

} // namespace

int main() {
    CelestialContracts();
    CalendarContracts();
    WeatherContracts();
    SimulationContracts();
    AssetContracts();
    ColorGradingContracts();
    LightProfileContracts();
    DecalContracts();
    SpotClusterContracts();
    RuntimeContracts();
    if (gFailures != 0) {
        std::fprintf(stderr, "Environment contracts failed: %d\n", gFailures);
        return EXIT_FAILURE;
    }
    std::printf("Environment contracts passed.\n");
    return EXIT_SUCCESS;
}
