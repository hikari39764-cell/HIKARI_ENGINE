#pragma once
#include <cstdint>
#include <string_view>

namespace HIKARI::SHADERPROFILE {
    // Assets の保存 enum と Gfx の compiler enum は ABI を保ち、許可表だけ共有する。
    // 未知 stage / profile は model=0 で拒否する。
    template<class Stage>
    constexpr bool ValidateShaderEntryProfile(Stage stage,std::string_view profile,uint32_t& model) noexcept {
        model=0;
        switch(stage) {
        case Stage::Vertex: if(profile=="vs_6_0") model=60; break;
        case Stage::Pixel: if(profile=="ps_6_0") model=60; break;
        case Stage::Compute: if(profile=="cs_6_0") model=60; else if(profile=="cs_6_5") model=65; break;
        case Stage::Amplification: if(profile=="as_6_5") model=65; break;
        case Stage::Mesh: if(profile=="ms_6_5") model=65; break;
        case Stage::Geometry: if(profile=="gs_6_0") model=60; break;
        default: break;
        }
        return model!=0;
    }
}
