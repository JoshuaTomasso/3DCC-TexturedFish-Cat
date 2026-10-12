// an ultra simple hlsl fragment shader
#pragma pack_matrix(row_major)

cbuffer CameraMatrices : register(b0)
{
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4 lightDirection;
    float4 lightColor;
    float4 cameraPosition;
}

// Use space1 to indicate descriptor set 1
Texture2D textures[] : register(t0, space1);
SamplerState samplers[] : register(s0, space1);

// Hard-coded material values as shown in the image
static float4 diffuse = { 1, 1, 1, 1 };
static float4 specular = { 1.0f, 1.0f, 1.0f, 1.0f };
static float4 ambient = { 0.75f, 0.75f, 0.75f, 1 };
static float4 emissive = { 0.0f, 0.0f, 0.0f, 1 };
static float Ns = 100;

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 tangent : TANGENT;
    float3 worldPos : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    // Sample the base color from the first texture
    float4 baseColorTexture = textures[0].Sample(samplers[0], input.texCoord);
    
    // Sample the roughness from the second texture (green channel contains roughness)
    float roughness = textures[1].Sample(samplers[0], input.texCoord).g;
    
    // Normalize the interpolated normal
    float3 normal = normalize(input.normal);
    
    // Calculate the light vector (opposite of light direction)
    float3 lightVector = -normalize(lightDirection.xyz);
    
    // Calculate the view vector (from point to camera)
    float3 viewVector = normalize(cameraPosition.xyz - input.worldPos);
    
    // Calculate the halfway vector for Blinn-Phong
    float3 halfwayVector = normalize(lightVector + viewVector);
    
    // Calculate the diffuse factor (Lambert's cosine law)
    float diffuseFactor = max(dot(normal, lightVector), 0.0);
    
    // Adjust specular power based on roughness (rougher = lower power)
    float adjustedNs = Ns * (1.0 - roughness * 0.9); // Scale to avoid going to zero
    
    // Calculate the specular factor with adjusted power
    float specularFactor = pow(max(dot(normal, halfwayVector), 0.0), adjustedNs);
    
    // Dampen specular intensity based on roughness (rougher = less specular)
    float specularIntensity = 1.0 - roughness;
    
    // Calculate the final color
    float3 ambientColor = ambient.xyz * baseColorTexture.rgb;
    float3 diffuseColor = diffuse.xyz * diffuseFactor * lightColor.xyz * baseColorTexture.rgb;
    float3 specularColor = specular.xyz * specularFactor * lightColor.xyz * specularIntensity;
    float3 emissiveColor = emissive.xyz;
    
    // Combine all lighting components
    float3 finalColor = ambientColor + diffuseColor + specularColor + emissiveColor;
    
    return float4(finalColor, baseColorTexture.a);
}