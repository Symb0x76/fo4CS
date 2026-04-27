NVIDIA Streamline runtime files are intentionally not distributed with this package.

Shared runtime folder
---------------------
Place the required NVIDIA Streamline / DLSS DLLs in this directory after installing the mod:

    Data\F4SE\Plugins\Streamline\

The FrameGen and Upscaler plugins both load Streamline runtime files from this shared directory.

Required for standalone NVIDIA Reflex
-------------------------------------
- sl.interposer.dll
- sl.common.dll
- sl.reflex.dll
- sl.pcl.dll

Required for DLSS Frame Generation / DLSS-G
-------------------------------------------
- sl.interposer.dll
- sl.common.dll
- sl.dlss_g.dll
- sl.reflex.dll
- sl.pcl.dll
- nvngx_dlssg.dll

Required for DLSS Upscaling
---------------------------
- sl.interposer.dll
- sl.common.dll
- sl.dlss.dll
- nvngx_dlss.dll

Sources
-------
- Streamline SDK DLLs come from NVIDIA Streamline SDK releases:
  https://github.com/NVIDIAGameWorks/Streamline/releases
- nvngx_dlss.dll / nvngx_dlssg.dll are installed by NVIDIA drivers on supported systems and may exist under C:\Windows\System32 on supported machines.

Do not redistribute these DLLs with this project unless you have confirmed your rights under NVIDIA's current redistribution terms.
