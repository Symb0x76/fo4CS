ShaderDB - Runtime Shader Replacement Database
================================================

Each subdirectory maps to a runtime version (PreNG, PostNG, PostAE, Common).
Inside each runtime directory, stage subdirectories contain shader entries.

Directory layout:
  ShaderDB/{Runtime}/{Stage}/{ShaderUID}/
    {ShaderUID}.txt   - metadata + optional replacement path

Entry format (.txt):
  [pixelHunt]
  asmHash=0x0ABC
  shader=Data\Shaders\SomeFeature\SomeShader.hlsl
  type=PS
  active=false

Important LightLimitFix rule:
  LLF must not be enabled from ShaderDB hash guessing. The current FO4 LLF
  direction follows Skyrim Community Shaders: validate the engine lighting hook,
  collect BSRenderPass scene lights, prove strict-light CB data, then bind b3 and
  t35-t37 only in the verified lighting shader path.

  The packaged PLACEHOLDER_light_ps entry is an inactive sentinel. Do not fill in
  asmHash, uncomment shader=, or set active=true as an LLF implementation step.
  Standalone LLF PS templates under Data\Shaders are support files only until
  a real FO4 lighting shader path is verified.

ShaderDB remains useful for generic shader replacement experiments and for
supporting evidence while mapping shader paths, but it is not the LLF route.
