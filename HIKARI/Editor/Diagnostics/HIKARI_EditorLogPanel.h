#pragma once
#include "Core/HIKARI_Logger.h"
#include <string>
#include <vector>

namespace HIKARI::EDITOR {
    class EditorLogPanel {
    public:
        void DrawContents();
    private:
        void Poll();
        void BuildRows();
        void OpenHistory();
        void ExportVisible();
        struct Row { CORE::LogRecord record; size_t count = 1; };
        std::vector<CORE::LogRecord> records_;
        std::vector<Row> rows_;
        uint64_t cursor_ = 0;
        uint64_t selected_ = 0;
        bool paused_ = false;
        bool follow_ = true;
        bool collapse_ = true;
        bool history_ = false;
        bool levels_[4]{true, true, true, true};
        char search_[192]{};
        std::string status_;
    };
}
