Community Shaders runtime data directory.

ShaderDB dump mode writes captured D3D11 shader bytecode under:
  Data\F4SE\Plugins\fo4CS\ShaderDump\<Runtime>

Each shader dump includes:
  <ShaderUID>.bin  - original bytecode
  <ShaderUID>.asm  - D3D disassembly
  <ShaderUID>.txt  - Shader.ini-style metadata for matching rules

Only PreNG ShaderDB generation is expected at this stage.
