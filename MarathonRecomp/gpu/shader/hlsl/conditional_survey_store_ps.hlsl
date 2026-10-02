// [Switch] SwitchSurveyPlainStore variant of conditional_survey_ps.hlsl, built (SPIR-V only) by
// tools/generate-switch-app-shaders.sh. Every fragment that passes the depth test stores 1 into the survey's
// counter instead of adding 1 to it: all of them hit the same address, where the atomic adds were serialised.
// The counter is only ever compared with zero (the conditional rendering prologue discards on 0; nothing reads
// it back), and it holds 0 before the first such fragment and non-zero after it either way.

#include "../../../../tools/XenosRecomp/XenosRecomp/shader_common.h"

#ifndef __spirv__

cbuffer SharedConstants : register(b2, space4)
{
	DEFINE_SHARED_CONSTANTS();
};

#endif

[earlydepthstencil]
float4 shaderMain() : SV_Target
{
    g_ConditionalSurveyBuffer[g_conditionalSurveyIndex] = 1;
    return float4(0.0, 0.0, 0.0, 0.0);
}
