// [Switch] SwitchSurveySlots variant of conditional_survey_ps.hlsl, built (SPIR-V only) by
// tools/generate-switch-app-shaders.sh. The renderer gives every survey a slot of g_ConditionalSurveyBuffer of its
// own and a generation that slot never held (gpu/video.cpp, SetConditionalSurveySlot), in the two words the port
// used for the survey and conditional rendering indices (g_conditionalSurveySlot, g_conditionalSurveyGeneration).
// Every fragment that passes the depth test stores the generation instead of counting: the conditional rendering
// prologue (SPEC_CONSTANT_SURVEY_SLOTS, in every pipeline then) discards where the slot does not hold it, where the
// port's discarded on a count of 0. Both mean that no sample of the survey has written.

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
    g_ConditionalSurveyBuffer[g_conditionalSurveySlot] = g_conditionalSurveyGeneration;
    return float4(0.0, 0.0, 0.0, 0.0);
}
