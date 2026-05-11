ShaderDB — Runtime Shader Replacement Database
================================================

Each subdirectory maps to a runtime version (PreNG, PostNG, PostAE, Common).
Inside each runtime directory, stage subdirectories contain shader entries.

Directory layout:
  ShaderDB/{Runtime}/{Stage}/{ShaderUID}/
    {ShaderUID}.txt   — metadata + replacement path

Example for replacing a pixel shader:
  ShaderDB/PreNG/Pixel/PS00AB01I0C2O6/
    PS00AB01I0C2O6.txt

Entry format (.txt):
  [pixelHunt]
  asmHash=0x0ABC
  shader=Data\Shaders\LightLimitFix\LightingPS.hlsl
  type=PS

How to generate entries:
1. Enable pipeline tracing: set bTracePipeline=true in CommunityShaders.ini
2. Run the game through scenes with dynamic lights
3. Find the trace output in the CommunityShaders log
4. For each PS that uses a light constant buffer (check asm disassembly):
   - Copy the dumped .txt and .asm files from ShaderDump/
   - Edit the .txt to add: shader=Data\Shaders\LightLimitFix\LightingPS.hlsl
   - Place in the correct runtime subdirectory

ASM hash identification:
  - The asm hash is computed from the shader's D3D disassembly (stripped of debug info)
  - Dumped shaders include the hash in their metadata
  - Match by checking CB slot 2 references in the .asm file
