// Общее начало всех шейдеров слоя rb (render/rb.h): константы вызова и различия GL и Vulkan
#ifdef VULKAN
layout(push_constant) uniform Push { uvec4 u_push[8]; };
#define RB_VERTEX_ID gl_VertexIndex
#define RB_INSTANCE_ID gl_InstanceIndex
#else
layout(location = 0) uniform uvec4 u_push[8];
#define RB_VERTEX_ID gl_VertexID
#define RB_INSTANCE_ID gl_InstanceID
#endif
