// an ultra simple hlsl vertex shader
#pragma pack_matrix(row_major)

cbuffer CameraMatrices : register(b0)
{
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4 lightDirection;
    float4 lightColor;
    float4 cameraPosition;
}

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 tangent : TANGENT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 tangent : TANGENT;
    float3 worldPos : TEXCOORD1;
};

PSInput main(VSInput input)
{
    PSInput output;
    
    // Transform position using view and projection matrices
    float4 worldPosition = float4(input.position, 1.0);
    float4 viewPosition = mul(worldPosition, viewMatrix);
    output.position = mul(viewPosition, projectionMatrix);
    
    // Pass world position for lighting calculations
    // Since we don't have a separate world matrix, input.position is our "world" position
    output.worldPos = input.position;
    
    // Pass through normal and texcoord
    output.normal = input.normal;
    output.texCoord = input.texCoord;
    output.tangent = input.tangent;
    
    return output;
}