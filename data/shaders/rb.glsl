// Общее начало всех шейдеров слоя rb (render/rb.h): константы вызова, номера вершины и экземпляра.
// Собирается офлайн в SPIR-V (tools/spv/build.sh)
#extension GL_ARB_shader_draw_parameters : require
// RB_VRS: конвейер шейдит по карте частоты, RB_FRAG_SIZE — размер блока этого фрагмента в пикселях
#ifdef RB_VRS
#extension GL_EXT_fragment_shading_rate : require
#define RB_FRAG_SIZE ivec2(1 << ((gl_ShadingRateEXT >> 2) & 3), 1 << (gl_ShadingRateEXT & 3))
#endif
layout(push_constant) uniform Push { uvec4 u_push[8]; };
#define RB_VERTEX_ID gl_VertexIndex
// gl_InstanceIndex считает и первый экземпляр вызова: номер внутри вызова — без него
#define RB_INSTANCE_ID (gl_InstanceIndex - gl_BaseInstanceARB)
