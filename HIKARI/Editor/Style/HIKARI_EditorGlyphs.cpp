#include "Editor/Style/HIKARI_EditorGlyphs.h"

#if defined(HIKARI_WITH_EDITOR)
#include <algorithm>
#include <array>
#include <cmath>

namespace HIKARI::EDITOR {
    namespace {
        struct GlyphCanvas {
            ImDrawList& drawList;
            ImVec2 center;
            float radius;
            ImU32 color;
            float thickness;

            ImVec2 Point(float x, float y) const noexcept {
                return {
                    center.x + x * radius,
                    center.y + y * radius
                };
            }

            void Line(float x0, float y0, float x1, float y1) const {
                drawList.AddLine(
                    Point(x0, y0),
                    Point(x1, y1),
                    color,
                    thickness);
            }

            void Rect(
                float x0,
                float y0,
                float x1,
                float y1,
                float rounding = 0.0f) const {
                drawList.AddRect(
                    Point(x0, y0),
                    Point(x1, y1),
                    color,
                    rounding * radius,
                    0,
                    thickness);
            }

            void FilledRect(
                float x0,
                float y0,
                float x1,
                float y1,
                float rounding = 0.0f) const {
                drawList.AddRectFilled(
                    Point(x0, y0),
                    Point(x1, y1),
                    color,
                    rounding * radius);
            }

            void Circle(float x, float y, float r, bool filled = false) const {
                if (filled) {
                    drawList.AddCircleFilled(Point(x, y), r * radius, color, 16);
                } else {
                    drawList.AddCircle(
                        Point(x, y),
                        r * radius,
                        color,
                        16,
                        thickness);
                }
            }

            void Polyline(
                const ImVec2* points,
                int count,
                bool closed = false) const {
                std::array<ImVec2, 8> transformed{};
                const int boundedCount = (std::min)(
                    count,
                    static_cast<int>(transformed.size()));
                for (int index = 0; index < boundedCount; ++index) {
                    transformed[static_cast<std::size_t>(index)] =
                        Point(points[index].x, points[index].y);
                }
                drawList.AddPolyline(
                    transformed.data(),
                    boundedCount,
                    color,
                    closed ? ImDrawFlags_Closed : ImDrawFlags_None,
                    thickness);
            }

            void Arc(float start, float end, float arcRadius = 0.72f) const {
                drawList.PathArcTo(
                    center,
                    arcRadius * radius,
                    start,
                    end,
                    16);
                drawList.PathStroke(color, ImDrawFlags_None, thickness);
            }
        };

        void DrawCircularArrow(
            const GlyphCanvas& canvas,
            bool clockwise) {
            constexpr float kPi = 3.14159265358979323846f;
            if (clockwise) {
                canvas.Arc(-0.80f * kPi, 0.58f * kPi);
                canvas.Line(0.54f, 0.28f, 0.76f, 0.58f);
                canvas.Line(0.76f, 0.58f, 0.39f, 0.59f);
            } else {
                canvas.Arc(0.42f * kPi, 1.80f * kPi);
                canvas.Line(-0.54f, 0.28f, -0.76f, 0.58f);
                canvas.Line(-0.76f, 0.58f, -0.39f, 0.59f);
            }
        }
    }

    void DrawEditorGlyph(
        ImDrawList& drawList,
        EditorGlyph glyph,
        const ImVec2& center,
        float size,
        ImU32 color,
        float thickness) {
        const GlyphCanvas canvas{
            drawList,
            center,
            (std::max)(4.0f, size * 0.5f),
            color,
            thickness
        };
        switch (glyph) {
        case EditorGlyph::Add:
            canvas.Line(-0.68f, 0.0f, 0.68f, 0.0f);
            canvas.Line(0.0f, -0.68f, 0.0f, 0.68f);
            break;
        case EditorGlyph::Close:
            canvas.Line(-0.55f, -0.55f, 0.55f, 0.55f);
            canvas.Line(0.55f, -0.55f, -0.55f, 0.55f);
            break;
        case EditorGlyph::More:
            canvas.Circle(-0.55f, 0.0f, 0.11f, true);
            canvas.Circle(0.0f, 0.0f, 0.11f, true);
            canvas.Circle(0.55f, 0.0f, 0.11f, true);
            break;
        case EditorGlyph::Play: {
            const ImVec2 points[]{
                { -0.38f, -0.68f },
                { 0.68f, 0.0f },
                { -0.38f, 0.68f }
            };
            canvas.Polyline(points, 3, true);
            break;
        }
        case EditorGlyph::Pause:
            canvas.FilledRect(-0.53f, -0.64f, -0.18f, 0.64f, 0.08f);
            canvas.FilledRect(0.18f, -0.64f, 0.53f, 0.64f, 0.08f);
            break;
        case EditorGlyph::Stop:
            canvas.FilledRect(-0.56f, -0.56f, 0.56f, 0.56f, 0.10f);
            break;
        case EditorGlyph::Save:
        case EditorGlyph::SaveAs:
            canvas.Rect(-0.70f, -0.72f, 0.70f, 0.72f, 0.10f);
            canvas.Rect(-0.38f, -0.72f, 0.34f, -0.18f, 0.04f);
            canvas.Rect(-0.40f, 0.18f, 0.40f, 0.72f, 0.05f);
            if (glyph == EditorGlyph::SaveAs) {
                canvas.Line(0.35f, 0.14f, 0.82f, 0.14f);
                canvas.Line(0.585f, -0.095f, 0.585f, 0.375f);
            }
            break;
        case EditorGlyph::Undo:
        case EditorGlyph::Redo:
        case EditorGlyph::Revert:
        case EditorGlyph::Refresh:
            DrawCircularArrow(
                canvas,
                glyph == EditorGlyph::Redo ||
                glyph == EditorGlyph::Refresh);
            break;
        case EditorGlyph::Import:
            canvas.Line(0.0f, -0.72f, 0.0f, 0.25f);
            canvas.Line(-0.38f, -0.05f, 0.0f, 0.33f);
            canvas.Line(0.38f, -0.05f, 0.0f, 0.33f);
            canvas.Line(-0.66f, 0.68f, 0.66f, 0.68f);
            canvas.Line(-0.66f, 0.68f, -0.66f, 0.34f);
            canvas.Line(0.66f, 0.68f, 0.66f, 0.34f);
            break;
        case EditorGlyph::Reimport:
            DrawCircularArrow(canvas, true);
            canvas.Line(0.0f, -0.42f, 0.0f, 0.31f);
            canvas.Line(-0.25f, 0.08f, 0.0f, 0.34f);
            canvas.Line(0.25f, 0.08f, 0.0f, 0.34f);
            break;
        case EditorGlyph::Dependency:
            canvas.Line(-0.45f, -0.38f, 0.46f, -0.38f);
            canvas.Line(-0.45f, -0.38f, 0.0f, 0.48f);
            canvas.Line(0.46f, -0.38f, 0.0f, 0.48f);
            canvas.Circle(-0.52f, -0.48f, 0.21f);
            canvas.Circle(0.52f, -0.48f, 0.21f);
            canvas.Circle(0.0f, 0.57f, 0.21f);
            break;
        case EditorGlyph::Inspector:
            canvas.Rect(-0.68f, -0.67f, 0.68f, 0.67f, 0.08f);
            canvas.Line(-0.42f, -0.34f, 0.42f, -0.34f);
            canvas.Line(-0.42f, 0.0f, 0.18f, 0.0f);
            canvas.Line(-0.42f, 0.34f, 0.35f, 0.34f);
            canvas.Circle(0.40f, 0.0f, 0.12f, true);
            break;
        case EditorGlyph::Settings:
            canvas.Circle(0.0f, 0.0f, 0.25f);
            canvas.Circle(0.0f, 0.0f, 0.62f);
            for (int index = 0; index < 8; ++index) {
                const float angle = static_cast<float>(index) *
                    3.14159265358979323846f * 0.25f;
                const float x0 = std::cos(angle) * 0.62f;
                const float y0 = std::sin(angle) * 0.62f;
                const float x1 = std::cos(angle) * 0.82f;
                const float y1 = std::sin(angle) * 0.82f;
                canvas.Line(x0, y0, x1, y1);
            }
            break;
        case EditorGlyph::Filter: {
            const ImVec2 points[]{
                { -0.72f, -0.58f },
                { 0.72f, -0.58f },
                { 0.22f, 0.02f },
                { 0.22f, 0.62f },
                { -0.22f, 0.42f },
                { -0.22f, 0.02f }
            };
            canvas.Polyline(points, 6, true);
            break;
        }
        case EditorGlyph::Grid:
            canvas.Rect(-0.70f, -0.70f, -0.10f, -0.10f, 0.08f);
            canvas.Rect(0.10f, -0.70f, 0.70f, -0.10f, 0.08f);
            canvas.Rect(-0.70f, 0.10f, -0.10f, 0.70f, 0.08f);
            canvas.Rect(0.10f, 0.10f, 0.70f, 0.70f, 0.08f);
            break;
        case EditorGlyph::List:
        case EditorGlyph::Compact:
            for (int row = -1; row <= 1; ++row) {
                const float y = static_cast<float>(row) * 0.48f;
                canvas.Circle(-0.58f, y, 0.08f, true);
                canvas.Line(-0.33f, y, 0.66f, y);
                if (glyph == EditorGlyph::Compact) {
                    canvas.Line(0.22f, y - 0.12f, 0.66f, y - 0.12f);
                }
            }
            break;
        case EditorGlyph::Folder: {
            const ImVec2 points[]{
                { -0.75f, -0.48f },
                { -0.18f, -0.48f },
                { 0.02f, -0.25f },
                { 0.75f, -0.25f },
                { 0.66f, 0.58f },
                { -0.75f, 0.58f }
            };
            canvas.Polyline(points, 6, true);
            break;
        }
        case EditorGlyph::Reveal: {
            const ImVec2 points[]{
                { -0.78f, 0.0f },
                { -0.38f, -0.42f },
                { 0.0f, -0.56f },
                { 0.38f, -0.42f },
                { 0.78f, 0.0f },
                { 0.38f, 0.42f },
                { 0.0f, 0.56f },
                { -0.38f, 0.42f }
            };
            canvas.Polyline(points, 8, true);
            canvas.Circle(0.0f, 0.0f, 0.20f);
            break;
        }
        case EditorGlyph::Delete:
            canvas.Rect(-0.48f, -0.32f, 0.48f, 0.68f, 0.08f);
            canvas.Line(-0.66f, -0.45f, 0.66f, -0.45f);
            canvas.Line(-0.25f, -0.68f, 0.25f, -0.68f);
            canvas.Line(-0.10f, -0.68f, -0.22f, -0.45f);
            canvas.Line(0.10f, -0.68f, 0.22f, -0.45f);
            canvas.Line(-0.18f, -0.12f, -0.18f, 0.42f);
            canvas.Line(0.18f, -0.12f, 0.18f, 0.42f);
            break;
        case EditorGlyph::Keyframe: {
            const ImVec2 points[]{
                { 0.0f, -0.72f },
                { 0.72f, 0.0f },
                { 0.0f, 0.72f },
                { -0.72f, 0.0f }
            };
            canvas.Polyline(points, 4, true);
            break;
        }
        case EditorGlyph::Transform:
            canvas.Line(-0.62f, 0.56f, 0.52f, 0.56f);
            canvas.Line(-0.62f, 0.56f, -0.62f, -0.58f);
            canvas.Line(0.52f, 0.56f, 0.24f, 0.34f);
            canvas.Line(0.52f, 0.56f, 0.24f, 0.76f);
            canvas.Line(-0.62f, -0.58f, -0.82f, -0.28f);
            canvas.Line(-0.62f, -0.58f, -0.41f, -0.28f);
            break;
        case EditorGlyph::Translate:
            canvas.Line(-0.72f, 0.0f, 0.72f, 0.0f);
            canvas.Line(0.0f, -0.72f, 0.0f, 0.72f);
            canvas.Line(-0.72f, 0.0f, -0.42f, -0.22f);
            canvas.Line(-0.72f, 0.0f, -0.42f, 0.22f);
            canvas.Line(0.72f, 0.0f, 0.42f, -0.22f);
            canvas.Line(0.72f, 0.0f, 0.42f, 0.22f);
            canvas.Line(0.0f, -0.72f, -0.22f, -0.42f);
            canvas.Line(0.0f, -0.72f, 0.22f, -0.42f);
            canvas.Line(0.0f, 0.72f, -0.22f, 0.42f);
            canvas.Line(0.0f, 0.72f, 0.22f, 0.42f);
            break;
        case EditorGlyph::Rotate:
            DrawCircularArrow(canvas, true);
            canvas.Circle(0.0f, 0.0f, 0.18f, true);
            break;
        case EditorGlyph::Scale:
            canvas.Line(-0.58f, 0.58f, 0.50f, -0.50f);
            canvas.Line(0.50f, -0.50f, 0.18f, -0.48f);
            canvas.Line(0.50f, -0.50f, 0.48f, -0.18f);
            canvas.Rect(-0.76f, 0.40f, -0.42f, 0.74f, 0.04f);
            canvas.Rect(0.40f, -0.74f, 0.74f, -0.40f, 0.04f);
            break;
        case EditorGlyph::WorldSpace:
            canvas.Circle(0.0f, 0.0f, 0.72f);
            canvas.Line(-0.72f, 0.0f, 0.72f, 0.0f);
            canvas.Arc(-1.18f, 1.18f, 0.38f);
            canvas.Arc(1.96f, 4.32f, 0.38f);
            break;
        case EditorGlyph::LocalSpace:
            canvas.Rect(-0.68f, -0.68f, 0.68f, 0.68f, 0.08f);
            canvas.Line(-0.38f, 0.38f, 0.42f, 0.38f);
            canvas.Line(-0.38f, 0.38f, -0.38f, -0.42f);
            canvas.Line(0.42f, 0.38f, 0.18f, 0.20f);
            canvas.Line(-0.38f, -0.42f, -0.56f, -0.18f);
            break;
        case EditorGlyph::Light:
            canvas.Circle(0.0f, 0.0f, 0.30f);
            for (int index = 0; index < 8; ++index) {
                const float angle = static_cast<float>(index) *
                    3.14159265358979323846f * 0.25f;
                canvas.Line(
                    std::cos(angle) * 0.50f,
                    std::sin(angle) * 0.50f,
                    std::cos(angle) * 0.78f,
                    std::sin(angle) * 0.78f);
            }
            break;
        case EditorGlyph::Gizmo:
            canvas.Circle(-0.34f, 0.34f, 0.12f, true);
            canvas.Line(-0.34f, 0.34f, 0.58f, 0.34f);
            canvas.Line(-0.34f, 0.34f, -0.34f, -0.58f);
            canvas.Line(-0.34f, 0.34f, 0.28f, -0.28f);
            canvas.Line(0.58f, 0.34f, 0.34f, 0.16f);
            canvas.Line(-0.34f, -0.58f, -0.52f, -0.34f);
            canvas.Line(0.28f, -0.28f, 0.00f, -0.26f);
            break;
        case EditorGlyph::Lens:
            canvas.Circle(-0.10f, -0.08f, 0.48f);
            canvas.Line(0.25f, 0.28f, 0.70f, 0.70f);
            canvas.Circle(-0.10f, -0.08f, 0.18f);
            break;
        case EditorGlyph::Camera:
            canvas.Rect(-0.72f, -0.42f, 0.38f, 0.52f, 0.10f);
            canvas.Circle(-0.18f, 0.05f, 0.27f);
            canvas.Line(0.38f, -0.20f, 0.75f, -0.48f);
            canvas.Line(0.75f, -0.48f, 0.75f, 0.48f);
            canvas.Line(0.75f, 0.48f, 0.38f, 0.20f);
            break;
        case EditorGlyph::Snap:
            canvas.Line(-0.58f, -0.62f, -0.58f, 0.18f);
            canvas.Line(0.58f, -0.62f, 0.58f, 0.18f);
            canvas.Arc(0.0f, 3.14159265358979323846f, 0.58f);
            canvas.Line(-0.58f, -0.62f, -0.20f, -0.62f);
            canvas.Line(0.20f, -0.62f, 0.58f, -0.62f);
            break;
        case EditorGlyph::Log:
            canvas.Rect(-0.64f, -0.72f, 0.64f, 0.72f, 0.06f);
            canvas.Line(-0.38f, -0.36f, 0.38f, -0.36f);
            canvas.Line(-0.38f, -0.04f, 0.38f, -0.04f);
            canvas.Line(-0.38f, 0.28f, 0.14f, 0.28f);
            canvas.Circle(0.40f, 0.30f, 0.08f, true);
            break;
        case EditorGlyph::Library:
            canvas.Rect(-0.72f, -0.68f, -0.18f, 0.68f, 0.05f);
            canvas.Rect(0.04f, -0.68f, 0.58f, 0.68f, 0.05f);
            canvas.Line(0.58f, -0.54f, 0.76f, -0.42f);
            canvas.Line(0.76f, -0.42f, 0.76f, 0.62f);
            break;
        case EditorGlyph::NewDocument:
            canvas.Rect(-0.62f, -0.72f, 0.42f, 0.72f, 0.05f);
            canvas.Line(0.08f, -0.72f, 0.42f, -0.38f);
            canvas.Line(0.08f, -0.72f, 0.08f, -0.38f);
            canvas.Line(0.08f, -0.38f, 0.42f, -0.38f);
            canvas.Line(0.26f, 0.32f, 0.80f, 0.32f);
            canvas.Line(0.53f, 0.05f, 0.53f, 0.59f);
            break;
        case EditorGlyph::Duplicate:
            canvas.Rect(-0.70f, -0.70f, 0.28f, 0.28f, 0.06f);
            canvas.Rect(-0.26f, -0.26f, 0.72f, 0.72f, 0.06f);
            break;
        case EditorGlyph::Focus:
            canvas.Circle(0.0f, 0.0f, 0.40f);
            canvas.Line(-0.82f, 0.0f, -0.38f, 0.0f);
            canvas.Line(0.38f, 0.0f, 0.82f, 0.0f);
            canvas.Line(0.0f, -0.82f, 0.0f, -0.38f);
            canvas.Line(0.0f, 0.38f, 0.0f, 0.82f);
            break;
        case EditorGlyph::Lock:
            canvas.Rect(-0.62f, -0.02f, 0.62f, 0.70f, 0.10f);
            canvas.Arc(
                3.14159265358979323846f,
                6.28318530717958647692f,
                0.42f);
            canvas.Line(-0.42f, 0.0f, -0.42f, -0.24f);
            canvas.Line(0.42f, 0.0f, 0.42f, -0.24f);
            canvas.Circle(0.0f, 0.31f, 0.10f, true);
            break;
        case EditorGlyph::Object: {
            const ImVec2 top[]{
                { 0.0f, -0.72f }, { 0.66f, -0.34f },
                { 0.0f, 0.04f }, { -0.66f, -0.34f }
            };
            canvas.Polyline(top, 4, true);
            canvas.Line(-0.66f, -0.34f, -0.66f, 0.34f);
            canvas.Line(0.66f, -0.34f, 0.66f, 0.34f);
            canvas.Line(-0.66f, 0.34f, 0.0f, 0.72f);
            canvas.Line(0.66f, 0.34f, 0.0f, 0.72f);
            canvas.Line(0.0f, 0.04f, 0.0f, 0.72f);
            break;
        }
        case EditorGlyph::Exit:
            canvas.Rect(-0.72f, -0.64f, 0.20f, 0.64f, 0.06f);
            canvas.Line(-0.12f, 0.0f, 0.76f, 0.0f);
            canvas.Line(0.76f, 0.0f, 0.42f, -0.34f);
            canvas.Line(0.76f, 0.0f, 0.42f, 0.34f);
            break;
        case EditorGlyph::File:
            canvas.Rect(-0.60f, -0.72f, 0.54f, 0.72f, 0.05f);
            canvas.Line(0.08f, -0.72f, 0.54f, -0.26f);
            canvas.Line(0.08f, -0.72f, 0.08f, -0.26f);
            canvas.Line(0.08f, -0.26f, 0.54f, -0.26f);
            canvas.Line(-0.34f, 0.10f, 0.30f, 0.10f);
            canvas.Line(-0.34f, 0.38f, 0.18f, 0.38f);
            break;
        case EditorGlyph::Texture:
            canvas.Rect(-0.72f, -0.62f, 0.72f, 0.62f, 0.08f);
            canvas.Circle(0.36f, -0.28f, 0.13f);
            canvas.Line(-0.58f, 0.40f, -0.16f, -0.02f);
            canvas.Line(-0.16f, -0.02f, 0.08f, 0.22f);
            canvas.Line(0.08f, 0.22f, 0.30f, 0.02f);
            canvas.Line(0.30f, 0.02f, 0.60f, 0.36f);
            break;
        case EditorGlyph::Model: {
            const ImVec2 front[] = {
                { -0.56f, -0.34f }, { 0.10f, -0.58f },
                { 0.58f, -0.18f }, { -0.08f, 0.08f }
            };
            const ImVec2 back[] = {
                { -0.58f, 0.16f }, { 0.08f, -0.08f },
                { 0.56f, 0.32f }, { -0.10f, 0.58f }
            };
            canvas.Polyline(front, 4, true);
            canvas.Polyline(back, 4, true);
            canvas.Line(-0.56f, -0.34f, -0.58f, 0.16f);
            canvas.Line(0.10f, -0.58f, 0.08f, -0.08f);
            canvas.Line(0.58f, -0.18f, 0.56f, 0.32f);
            canvas.Line(-0.08f, 0.08f, -0.10f, 0.58f);
            break;
        }
        case EditorGlyph::Material:
            canvas.Circle(0.0f, 0.0f, 0.68f);
            canvas.Arc(-1.57f, 1.57f, 0.42f);
            canvas.Circle(-0.22f, -0.22f, 0.10f, true);
            break;
        case EditorGlyph::Scene:
            canvas.Line(-0.74f, 0.56f, 0.74f, 0.56f);
            canvas.Rect(-0.62f, -0.20f, -0.12f, 0.56f, 0.04f);
            canvas.Rect(0.02f, -0.54f, 0.60f, 0.56f, 0.04f);
            canvas.Line(0.14f, -0.20f, 0.48f, -0.20f);
            canvas.Line(0.14f, 0.10f, 0.48f, 0.10f);
            break;
        case EditorGlyph::Sky:
            canvas.Arc(3.14159265358979323846f, 6.28318530717958647692f, 0.50f);
            canvas.Line(-0.76f, 0.46f, 0.76f, 0.46f);
            canvas.Line(-0.50f, 0.18f, -0.72f, 0.02f);
            canvas.Line(0.50f, 0.18f, 0.72f, 0.02f);
            canvas.Line(0.0f, -0.50f, 0.0f, -0.76f);
            break;
        case EditorGlyph::Vfx:
            canvas.Line(0.0f, -0.76f, 0.0f, 0.76f);
            canvas.Line(-0.76f, 0.0f, 0.76f, 0.0f);
            canvas.Line(-0.52f, -0.52f, 0.52f, 0.52f);
            canvas.Line(0.52f, -0.52f, -0.52f, 0.52f);
            canvas.Circle(0.0f, 0.0f, 0.18f);
            break;
        case EditorGlyph::Sequence:
            canvas.Rect(-0.72f, -0.56f, 0.72f, 0.56f, 0.06f);
            canvas.Line(-0.40f, -0.56f, -0.40f, 0.56f);
            canvas.Line(0.40f, -0.56f, 0.40f, 0.56f);
            canvas.Line(-0.72f, -0.22f, -0.40f, -0.22f);
            canvas.Line(-0.72f, 0.22f, -0.40f, 0.22f);
            canvas.Line(0.40f, -0.22f, 0.72f, -0.22f);
            canvas.Line(0.40f, 0.22f, 0.72f, 0.22f);
            break;
        case EditorGlyph::Animation:
            canvas.Circle(-0.40f, -0.44f, 0.13f, true);
            canvas.Circle(0.36f, -0.16f, 0.13f, true);
            canvas.Circle(-0.18f, 0.48f, 0.13f, true);
            canvas.Line(-0.40f, -0.44f, 0.36f, -0.16f);
            canvas.Line(0.36f, -0.16f, -0.18f, 0.48f);
            canvas.Line(-0.18f, 0.48f, -0.40f, -0.44f);
            break;
        case EditorGlyph::StateMachine:
            canvas.Rect(-0.76f, -0.64f, -0.16f, -0.14f, 0.10f);
            canvas.Rect(0.16f, 0.14f, 0.76f, 0.64f, 0.10f);
            canvas.Line(-0.16f, -0.38f, 0.46f, 0.14f);
            canvas.Line(0.46f, 0.14f, 0.18f, 0.10f);
            canvas.Line(0.46f, 0.14f, 0.38f, -0.12f);
            break;
        case EditorGlyph::Audio:
            canvas.Rect(-0.70f, -0.26f, -0.38f, 0.26f, 0.04f);
            canvas.Line(-0.38f, -0.26f, 0.0f, -0.58f);
            canvas.Line(0.0f, -0.58f, 0.0f, 0.58f);
            canvas.Line(0.0f, 0.58f, -0.38f, 0.26f);
            canvas.Arc(-0.70f, 0.70f, 0.42f);
            canvas.Arc(-0.70f, 0.70f, 0.70f);
            break;
        }
    }

} // namespace HIKARI::EDITOR
#endif
