# NVIDIA Streamline SDK

HIKARI uses NVIDIA Streamline as an optional runtime backend for DLAA, DLSS
Super Resolution, Reflex, and DLSS Frame Generation. The SDK is intentionally
not committed because its signed runtime package is large and must remain an
explicit local dependency.

Expected local layout:

```text
ThirdParty/Streamline/sdk/
  include/
  lib/x64/sl.interposer.lib
  bin/x64/sl.interposer.dll
  bin/x64/sl.common.dll
  bin/x64/sl.dlss.dll
  bin/x64/nvngx_dlss.dll
  bin/x64/nvngx_dlss.license.txt
```

Frame generation and Reflex are optional capabilities discovered at runtime.
To enable their platform path, add the signed files from the same Streamline
release package:

```text
  bin/x64/sl.dlss_g.dll
  bin/x64/sl.reflex.dll
  bin/x64/sl.pcl.dll
  bin/x64/nvngx_dlssg.dll
  bin/x64/reflex.license.txt
```

HIKARI loads and copies only the optional plugins that are present. Missing
frame-generation plugins do not disable the DLSS/DLAA backend. Game exports
deploy the same signed runtime set from the selected build output and fail with
a concrete missing-dependency error when an enabled feature is incomplete.

The validated package is `streamline-sdk-v2.12.0.zip` from the official
NVIDIA-RTX/Streamline GitHub release. When this layout is absent, HIKARI builds
without Streamline and reports DLSS/DLAA as unavailable instead of silently
enabling a placeholder path.
