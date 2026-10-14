#pragma once

namespace HIKARI {

class PerformanceAuditPanel {
public:
    void Draw(bool& open, bool revealRequested) const;
    void DrawContents() const;
};

} // namespace HIKARI
