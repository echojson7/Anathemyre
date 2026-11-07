#version 460
// Visibility buffer, сетки: номер = (видимый кластер + 1) << 7 | треугольник кластера
layout(location = 0) flat in uint v_slot;
layout(location = 0) out uint o_id;

void main() { o_id = ((v_slot + 1u) << 7) | uint(gl_PrimitiveID); }
