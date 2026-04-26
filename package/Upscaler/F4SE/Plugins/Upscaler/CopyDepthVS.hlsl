struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 Texcoord : TEXCOORD0;
};

VS_OUTPUT main(uint vertexID : SV_VertexID)
{
	VS_OUTPUT output;

	output.Texcoord.x = (vertexID == 2) ? 2.0 : 0.0;
	output.Texcoord.y = (vertexID == 1) ? -1.0 : 1.0;

	output.Position.x = output.Texcoord.x * 2.0 - 1.0;
	output.Position.y = -output.Texcoord.y * 2.0 + 1.0;
	output.Position.z = 0.0;
	output.Position.w = 1.0;

	return output;
}
