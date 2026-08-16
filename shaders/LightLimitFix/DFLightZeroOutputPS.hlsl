// Zero-output DFLight forward pass. Bound to every point-light pass after the
// first one of the frame: the first pass (DFLightForwardConsumerPS) already
// emits the full LLF clustered light list, so the remaining vanilla per-light
// passes must contribute nothing to avoid double-counting.
//
// Signature matches the vanilla DFLight forward pass (one SV_POSITION input,
// two targets).

void main(
	float4 v0 : SV_POSITION0,
	out float4 o0 : SV_Target0,
	out float4 o1 : SV_Target1)
{
	o0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	o1 = float4(0.0f, 0.0f, 0.0f, 1.0f);
}
