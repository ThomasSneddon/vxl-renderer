/*
I don't understantd it!
*/

cbuffer game_normals : register(b4)
{
    float4 normal_table[245];
}

cbuffer hva_buffer_data : register(b5)
{
    float4 vxl_dimension_scale;
    float4 vxl_minbounds;
    float4 vxl_maxbounds;
    float4 light_dir;
    row_major float4x4 transformation_matrix;
    float4 remap_color;
    float section_buffer_size;
};

struct vxl_buffer_decl
{
    uint color, normal, x, y, z;
};

RWTexture2D<float2> render_target : register(u1);
//RWTexture3D<uint2> vxl_data : register(u2);
//StructuredBuffer<vxl_buffer_decl> vxl_data : register(t2);
Texture2D<uint> vxl_data : register(t2);
Texture1D<uint> vpl_data : register(t3);
Texture1D<uint> pal_data : register(t6);

cbuffer state_data : register(b7)
{
    float4x4 _world;
    float4 _light;
    float4 remap;
    float4 scale_factor;
    float4 bgcolor;
}

float3 vxl_projection(float3 model_pos)
{
    const float w = 256.0f;
    const float h = 256.0f;
    const float f = 5000.0f;
    
    float3 result = 0.0f.xxx;
    
    result.x = w / 2.0 + (model_pos.x - model_pos.y) / sqrt(2.0);
    result.y = h / 2.0 + (model_pos.x + model_pos.y) / sqrt(8.0) - model_pos.z * sqrt(3.0) / 2.0;
    result.z = sqrt(3.0) / 2.0 / f * (4000.0 * sqrt(2.0) / 3.0 - (model_pos.x + model_pos.y) / sqrt(2.0) - model_pos.z / sqrt(3.0));
    
    return result;
}

static const float Epsilon = 1e-10;
static const float pi = 3.1415926536;

float3 RGBtoHCV(in float3 RGB)
{
    // Based on work by Sam Hocevar and Emil Persson
    float4 P = (RGB.g < RGB.b) ? float4(RGB.bg, -1.0, 2.0 / 3.0) : float4(RGB.gb, 0.0, -1.0 / 3.0);
    float4 Q = (RGB.r < P.x) ? float4(P.xyw, RGB.r) : float4(RGB.r, P.yzx);
    float C = Q.x - min(Q.w, Q.y);
    float H = abs((Q.w - Q.y) / (6 * C + Epsilon) + Q.z);
    return float3(H, C, Q.x);
}

float3 RGBtoHSV(in float3 RGB)
{
    float3 HCV = RGBtoHCV(RGB);
    float S = HCV.y / (HCV.z + Epsilon);
    return float3(HCV.x, S, HCV.z);
}

float3 HUEtoRGB(in float H)
{
    float R = abs(H * 6 - 3) - 1;
    float G = 2 - abs(H * 6 - 2);
    float B = 2 - abs(H * 6 - 4);
    return saturate(float3(R, G, B));
}

float3 HSVtoRGB(in float3 HSV)
{
    float3 RGB = HUEtoRGB(HSV.x);
    return ((RGB - 1) * HSV.y + 1) * HSV.z;
}

float clamp_zero(float f)
{
    return f > 0.0f ? f : 0.0f;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    static const uint max_byte_per_row = 16000;
    uint width = 0;
    
    vxl_buffer_decl voxel;
    uint byte_address = DTid.x * 5;
    if (byte_address >= section_buffer_size)
        return;
    
    uint row = byte_address / max_byte_per_row;
    uint xoffset = byte_address - row * max_byte_per_row;
    voxel.color = vxl_data[uint2(xoffset, row)];
    voxel.normal = vxl_data[uint2(xoffset + 1, row)];
    voxel.x = vxl_data[uint2(xoffset + 2, row)];
    voxel.y = vxl_data[uint2(xoffset + 3, row)];
    voxel.z = vxl_data[uint2(xoffset + 4, row)];
    
    float4 modelspace_pos = float4(voxel.x, voxel.y, voxel.z, 1.0f);
    float4 modelspace_nrm = float4(normal_table[voxel.normal].xyz, 0.0f) * float4(1.0f, -1.0f, 1.0f, 1.0f);
    float4 voxel_pos = mul(modelspace_pos, transformation_matrix);
    float3 n = normalize(mul(modelspace_nrm, transformation_matrix).xyz);
    float3 proj_voxel_pos = vxl_projection(voxel_pos.xyz);
    float bufferz = render_target[proj_voxel_pos.xy].g;
    
    if ((bufferz != 0.0f && bufferz < proj_voxel_pos.z)/* || voxel.r == 0*/)
        return;
    
    static const float3 u = float3(0.0f, 0.0f, 1.0f);
    static const float d = 3.0f;
    
    float3 l = normalize(light_dir.xyz);
    float3 l2 = length(l + u) == 0.0f ? 0.0f.xxx : normalize(l + u);
    float f1 = dot(n, l);
    float f2 = dot(n, l2) / (d - (d - 1.0f) * dot(n, l2));
    uint i = 16 * (clamp_zero(f1) + clamp_zero(f2));
    uint real_color_index = vpl_data[i * 256 + voxel.color];
    
    render_target[proj_voxel_pos.xy] = float2(real_color_index, proj_voxel_pos.z);
}

struct vs_output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

vs_output vmain(float3 pos : POSITION, float2 uv : TEXCOORD)
{
    vs_output output;
    
    output.position = float4(pos, 1.0f);
    output.uv = uv;
    
    return output;
}

float4 pmain(vs_output input) : SV_Target
{
    uint2 pix_coord = input.uv * 256.0f.xx;
    uint color_idx = render_target[pix_coord].x;
    uint color_offset = color_idx * 3;
    float3 color = float3(pal_data[color_offset], pal_data[color_offset + 1], pal_data[color_offset + 2]);
    if (color_idx >= 16 && color_idx < 32)
    {
        float i = color_idx - 16;
        color.rgb = RGBtoHSV(remap.rgb / 255.0f);
        color.r = color.r;
        color.g = color.g * sin(i * pi / 67.5f + pi / 3.6f);
        color.b = color.b * cos(i * 7.0f * pi / 270.0f + pi / 9.0f);
        color.rgb = HSVtoRGB(color.rgb);
        return float4(color, 1.0f);
    }
    
    return float4(color / 255.0f, 1.0f);
}

struct box_vert_output
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

box_vert_output box_vmain(float4 position : POSITION, uint instance_id : SV_InstanceID)
{
    static const uint max_byte_per_row = 16000;
    
    vxl_buffer_decl voxel;
    uint byte_address = instance_id * 5;
    
    uint row = byte_address / max_byte_per_row;
    uint xoffset = byte_address - row * max_byte_per_row;
    voxel.color = vxl_data[uint2(xoffset, row)];
    voxel.normal = vxl_data[uint2(xoffset + 1, row)];
    voxel.x = vxl_data[uint2(xoffset + 2, row)];
    voxel.y = vxl_data[uint2(xoffset + 3, row)];
    voxel.z = vxl_data[uint2(xoffset + 4, row)];
    
    float4 modelspace_pos = float4(voxel.x, voxel.y, voxel.z, 1.0f);
    modelspace_pos.xyz += position.xyz;
    
    float4 modelspace_nrm = float4(normal_table[voxel.normal].xyz, 0.0f) * float4(1.0f, -1.0f, 1.0f, 1.0f);
    float4 voxel_pos = mul(modelspace_pos, transformation_matrix);
    voxel_pos.xyz *= scale_factor.xyz;
    
    float3 n = normalize(mul(modelspace_nrm, transformation_matrix).xyz);
    float3 proj_voxel_pos = vxl_projection(voxel_pos.xyz);
    
    static const float3 u = float3(0.0f, 0.0f, 1.0f);
    static const float d = 3.0f;
    
    float3 l = normalize(light_dir.xyz);
    float3 l2 = length(l + u) == 0.0f ? 0.0f.xxx : normalize(l + u);
    float f1 = dot(n, l);
    float f2 = dot(n, l2) / (d - (d - 1.0f) * dot(n, l2));
    uint i = 16 * (clamp_zero(f1) + clamp_zero(f2));
    uint real_color_index = vpl_data[i * 256 + voxel.color];
    float4 real_color;
    
    uint color_offset = real_color_index * 3;
    float3 color = float3(pal_data[color_offset], pal_data[color_offset + 1], pal_data[color_offset + 2]);
    if (real_color_index >= 16 && real_color_index < 32)
    {
        float i = real_color_index - 16;
        color.rgb = RGBtoHSV(remap.rgb / 255.0f);
        color.r = color.r;
        color.g = color.g * sin(i * pi / 67.5f + pi / 3.6f);
        color.b = color.b * cos(i * 7.0f * pi / 270.0f + pi / 9.0f);
        color.rgb = HSVtoRGB(color.rgb);
        real_color = float4(color, 1.0f);
    }
    else
    {
        real_color = float4(color / 255.0f, 1.0f);
    }
    
    proj_voxel_pos.xy /= 256.0f / 2.0f;
    proj_voxel_pos.xy -= 1.0f.xx;
    proj_voxel_pos.y *= -1.0f;
    proj_voxel_pos.z += 0.5f;
    box_vert_output output;
    
    output.position = float4(proj_voxel_pos, 1.0f);
    output.color = real_color;
    
    return output;
}

float4 box_pmain(box_vert_output input) : SV_Target
{
    return input.color;
}