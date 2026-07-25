#pragma once

// Compatibility umbrella for existing engine consumers.
// Runtime state and implementations live in semantic Runtime modules.
#include "HIKARI_Core.h"
#include "HIKARI_2D.h"
#include "HIKARI_3D.h"

#include "HIKARI_Bgm.h"
#include "HIKARI_SE.h"
#include "HIKARI_Anim.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Runtime/HIKARI_RuntimeHost.h"
#include "Runtime/HIKARI_GamePresentationController.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"
#include "Runtime/Host/HIKARI_IEditorUiServices.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Vfx/Runtime/HIKARI_VfxSystem.h"

#include "Platform/HIKARI_Win32Window.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_Dx12Core.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Material/HIKARI_DefaultPbrResources.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Temporal/HIKARI_TaaResolvePass.h"
#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"
#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMaskPass.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGenerationPolicy.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Audio/HIKARI_Audio.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
