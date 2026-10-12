// minimalistic code to draw a single triangle, this is not part of the API.
#include "shaderc/shaderc.h" // needed for compiling shaders at runtime
#define TINYGLTF_IMPLEMENTATION 
#define STB_IMAGE_IMPLEMENTATION 
#define STB_IMAGE_WRITE_IMPLEMENTATION 
#include "tiny_gltf.h" 
#include "Camera.h"
#include "TextureUtils.h"
#ifdef _WIN32 // must use MT platform DLL libraries on windows
#pragma comment(lib, "shaderc_combined.lib") 
#endif

void PrintLabeledDebugString(const char* label, const char* toPrint)
{
	std::cout << label << toPrint << std::endl;

	//OutputDebugStringA is a windows-only function 
#if defined WIN32 
	OutputDebugStringA(label);
	OutputDebugStringA(toPrint);
#endif
}

class Renderer
{
	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	VkRenderPass renderPass;
	GW::CORE::GEventReceiver shutdown;

	// Input proxy handles
	GW::INPUT::GInput keyboard; // For keyboard and mouse input
	GW::INPUT::GController controller; // For game controller input

	// what we need at a minimum to draw a triangle
	VkDevice device = nullptr;
	VkPhysicalDevice physicalDevice = nullptr;
	VkBuffer vertexHandle = nullptr;
	VkDeviceMemory vertexData = nullptr;
	VkShaderModule vertexShader = nullptr;
	VkShaderModule fragmentShader = nullptr;
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;

	// TinyGLTF model instance
	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF loader;
	bool modelLoaded = false;

	// Geometry buffer (unified for both vertex and index data)
	VkBuffer geometryBuffer = nullptr;
	VkDeviceMemory geometryData = nullptr;
	uint32_t indexCount = 0;
	bool hasIndexBuffer = false;

	// Inside the Renderer class declaration, add these member variables:
	VkIndexType indexType = VK_INDEX_TYPE_UINT16;
	VkDeviceSize indexBufferOffset = 0;
	VkDeviceSize vertexBufferOffset = 0;
	uint32_t vertexStride = 0;
	uint32_t vertexCount = 0;

	// Add these inside the Renderer class
	struct VertexAttributeInfo {
		int accessorIndex = -1;
		VkDeviceSize byteOffset = 0;
		uint32_t stride = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
	};

	// Store information for each attribute
	VertexAttributeInfo positionInfo;
	VertexAttributeInfo normalInfo;
	VertexAttributeInfo texCoordInfo;
	VertexAttributeInfo tangentInfo;

	struct UniformBufferObject {
		GW::MATH::GMATRIXF viewMatrix;
		GW::MATH::GMATRIXF projectionMatrix;
		GW::MATH::GVECTORF lightDirection;
		GW::MATH::GVECTORF lightColor;
		GW::MATH::GVECTORF cameraPosition;
	};

	VkBuffer uniformBuffer = nullptr;
	VkDeviceMemory uniformBufferMemory = nullptr;
	VkDescriptorSetLayout descriptorSetLayout = nullptr;
	VkDescriptorPool descriptorPool = nullptr;
	VkDescriptorSet descriptorSet = nullptr;

	// Camera matrices
	GW::MATH::GMATRIXF viewMatrix;
	GW::MATH::GMATRIXF projectionMatrix;

	// Texture-related members
	struct TextureData
	{
		VkBuffer buffer;      
		VkDeviceMemory memory;
		VkImage image;
		VkImageView imageView;
	};

	std::vector<TextureData> textures;
	VkSampler textureSampler = nullptr;
	bool hasTexture = false;
	VkDescriptorSetLayout textureSetLayout = nullptr;
	VkDescriptorSet textureDescriptorSet = nullptr;
	static constexpr uint32_t MAX_TEXTURE_DESCRIPTORS = 16; // Maximum number of textures we can support

	unsigned int windowWidth, windowHeight;


public:
	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk)
	{
		win = _win;
		vlk = _vlk;

		UpdateWindowDimensions();
		GetHandlesFromSurface(); // Get handles before loading model

		// Load model first, then initialize graphics with correct vertex format
		if (LoadGLTFModel("../Models/blenderFish.gltf"))
		{
			PrintLabeledDebugString("GLTF model loaded successfully", "");
			// Try to load texture from the model
			LoadTextureFromModel();
		}
		else
		{
			PrintLabeledDebugString("Falling back to hardcoded triangle", "");
			InitializeVertexBuffer(); // Only create fallback buffer if model loading fails
		}

		// Initialize uniform buffer and descriptor sets
		CreateDescriptorSetLayout();
		CreateTextureDescriptorSetLayout();
		CreateUniformBuffer();
		CreateDescriptorPool();
		CreateDescriptorSet();
		CreateTextureDescriptorSet();

		CompileShaders();
		InitializeGraphicsPipeline();
		BindShutdownCallback();
	}

private:
	void UpdateWindowDimensions()
	{
		win.GetClientWidth(windowWidth);
		win.GetClientHeight(windowHeight);
	}

	void InitializeGraphics()
	{
		GetHandlesFromSurface();
		InitializeVertexBuffer();
		CompileShaders();
		InitializeGraphicsPipeline();
	}

	void UpdateViewMatrix(float deltaTime)
	{
		viewMatrix = FreeLookCamera(win);

		// Extract camera position from the inverse of the view matrix
		// In a view matrix, the camera position can be extracted from the translation component
		// of the inverse view matrix
		GW::MATH::GMATRIXF inverseView;
		GW::MATH::GMatrix matrixMath;
		matrixMath.InverseF(viewMatrix, inverseView);

		// The camera position is in the last row of the inverse view matrix
		// (or we could just extract the translation component directly)
		GW::MATH::GVECTORF extractedCameraPos = {
			inverseView.row4.x,
			inverseView.row4.y,
			inverseView.row4.z,
			1.0f
		};

	}

	bool LoadTextureFromModel() {
		// Check if the model has materials and textures
		if (gltfModel.materials.empty()) {
			PrintLabeledDebugString("No materials found in model", "");
			return false;
		}

		// Get the first material
		const tinygltf::Material& material = gltfModel.materials[0];
		bool textureLoaded = false;

		// Create a sampler for the textures (we'll use one sampler for all textures)
		if (textureSampler == nullptr) {
			CreateSampler(vlk, textureSampler, VK_SAMPLER_ADDRESS_MODE_REPEAT);
		}

		// Load base color texture if available
		if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
			// Get the texture index
			int textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;

			if (textureIndex < gltfModel.textures.size()) {
				// Get the texture
				const tinygltf::Texture& texture = gltfModel.textures[textureIndex];

				// Get the image index
				int imageIndex = texture.source;

				if (imageIndex >= 0 && imageIndex < gltfModel.images.size()) {
					// Get the image
					const tinygltf::Image& image = gltfModel.images[imageIndex];

					PrintLabeledDebugString("Loading base color texture: ", image.name.c_str());

					// Create a new TextureData object to store our texture
					TextureData baseColorTexture = {};

					// Upload the texture to the GPU (using sRGB color space for better color representation)
					UploadTextureToGPU(vlk, image, baseColorTexture.memory, baseColorTexture.image,
						baseColorTexture.imageView, false);

					// Add the texture to our vector
					textures.push_back(baseColorTexture);
					textureLoaded = true;

					PrintLabeledDebugString("Base color texture loaded successfully", "");
				}
			}
		}

		// Load metallic roughness texture if available
		if (material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
			// Get the texture index
			int textureIndex = material.pbrMetallicRoughness.metallicRoughnessTexture.index;

			if (textureIndex < gltfModel.textures.size()) {
				// Get the texture
				const tinygltf::Texture& texture = gltfModel.textures[textureIndex];

				// Get the image index
				int imageIndex = texture.source;

				if (imageIndex >= 0 && imageIndex < gltfModel.images.size()) {
					// Get the image
					const tinygltf::Image& image = gltfModel.images[imageIndex];

					PrintLabeledDebugString("Loading metallic roughness texture: ", image.name.c_str());

					// Create a new TextureData object to store our texture
					TextureData metallicRoughnessTexture = {};

					// Upload the texture to the GPU (using linear color space for PBR textures)
					UploadTextureToGPU(vlk, image, metallicRoughnessTexture.memory, metallicRoughnessTexture.image,
						metallicRoughnessTexture.imageView, true);

					// Add the texture to our vector
					textures.push_back(metallicRoughnessTexture);
					textureLoaded = true;

					PrintLabeledDebugString("Metallic roughness texture loaded successfully", "");
				}
			}
		}

		// Set hasTexture flag based on whether any texture was successfully loaded
		hasTexture = !textures.empty();

		return textureLoaded;
	}

	bool LoadGLTFModel(const std::string& filename)
	{
		std::string err;
		std::string warn;

		// Load the model
		modelLoaded = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filename);

		if (!warn.empty()) {
			PrintLabeledDebugString("GLTF Warning: ", warn.c_str());
		}

		if (!err.empty()) {
			PrintLabeledDebugString("GLTF Error: ", err.c_str());
			return false;
		}

		if (!modelLoaded) {
			PrintLabeledDebugString("Failed to load GLTF model: ", filename.c_str());
			return false;
		}

		// Process the model data
		if (!ProcessGLTFModelData()) {
			PrintLabeledDebugString("Failed to process GLTF model data", "");
			return false;
		}

		PrintLabeledDebugString("Successfully loaded GLTF model: ", filename.c_str());
		return true;
	}

	void CreateGeometryBuffer(const void* data, unsigned int sizeInBytes, bool hasIndices = false)
	{
		// Create a single buffer for both vertex and index data
		GvkHelper::create_buffer(physicalDevice, device, sizeInBytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&geometryBuffer, &geometryData);

		// Transfer data to the buffer
		GvkHelper::write_to_buffer(device, geometryData, data, sizeInBytes);

		// Track if we have index data
		this->hasIndexBuffer = hasIndices;
	}

	bool ProcessGLTFModelData()
	{
		// Make sure we have at least one mesh
		if (gltfModel.meshes.empty()) {
			return false;
		}

		// Get the first mesh (for simplicity)
		const tinygltf::Mesh& mesh = gltfModel.meshes[0];
		if (mesh.primitives.empty()) {
			return false;
		}

		// Get the first primitive
		const tinygltf::Primitive& primitive = mesh.primitives[0];

		// Get the buffer that contains the data (assuming all in one buffer)
		const tinygltf::Buffer& buffer = gltfModel.buffers[0];

		// Process position attribute
		auto positionIt = primitive.attributes.find("POSITION");
		if (positionIt != primitive.attributes.end()) {
			positionInfo.accessorIndex = positionIt->second;
			const tinygltf::Accessor& accessor = gltfModel.accessors[positionInfo.accessorIndex];
			const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
			positionInfo.byteOffset = bufferView.byteOffset;
			positionInfo.stride = accessor.ByteStride(bufferView);
			positionInfo.format = VK_FORMAT_R32G32B32_SFLOAT; // Position is VEC3
			vertexCount = accessor.count;

			//PrintLabeledDebugString("Position stride: ", std::to_string(positionInfo.stride).c_str());
			//PrintLabeledDebugString("Position buffer view offset: ", std::to_string(bufferView.byteOffset).c_str());
		}
		else {
			return false; // Position is required
		}

		// Process normal attribute
		auto normalIt = primitive.attributes.find("NORMAL");
		if (normalIt != primitive.attributes.end()) {
			normalInfo.accessorIndex = normalIt->second;
			const tinygltf::Accessor& accessor = gltfModel.accessors[normalInfo.accessorIndex];
			const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
			normalInfo.byteOffset = bufferView.byteOffset;
			normalInfo.stride = accessor.ByteStride(bufferView);
			normalInfo.format = VK_FORMAT_R32G32B32_SFLOAT; // Normal is VEC3

			//PrintLabeledDebugString("Normal stride: ", std::to_string(normalInfo.stride).c_str());
			//PrintLabeledDebugString("Normal buffer view offset: ", std::to_string(bufferView.byteOffset).c_str());
		}

		// Process texcoord attribute
		auto texcoordIt = primitive.attributes.find("TEXCOORD_0");
		if (texcoordIt != primitive.attributes.end()) {
			texCoordInfo.accessorIndex = texcoordIt->second;
			const tinygltf::Accessor& accessor = gltfModel.accessors[texCoordInfo.accessorIndex];
			const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
			texCoordInfo.byteOffset = bufferView.byteOffset;
			texCoordInfo.stride = accessor.ByteStride(bufferView);
			texCoordInfo.format = VK_FORMAT_R32G32_SFLOAT; // TexCoord is VEC2

			//PrintLabeledDebugString("TexCoord stride: ", std::to_string(texCoordInfo.stride).c_str());
			//PrintLabeledDebugString("TexCoord buffer view offset: ", std::to_string(bufferView.byteOffset).c_str());
		}

		// Process tangent attribute
		auto tangentIt = primitive.attributes.find("TANGENT");
		if (tangentIt != primitive.attributes.end()) {
			tangentInfo.accessorIndex = tangentIt->second;
			const tinygltf::Accessor& accessor = gltfModel.accessors[tangentInfo.accessorIndex];
			const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
			tangentInfo.byteOffset = bufferView.byteOffset;
			tangentInfo.stride = accessor.ByteStride(bufferView);
			tangentInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT; // Tangent is VEC4

			//PrintLabeledDebugString("Tangent stride: ", std::to_string(tangentInfo.stride).c_str());
			//PrintLabeledDebugString("Tangent buffer view offset: ", std::to_string(bufferView.byteOffset).c_str());
		}

		// Process indices
		bool hasIndices = (primitive.indices >= 0);
		if (hasIndices) {
			const tinygltf::Accessor& indexAccessor = gltfModel.accessors[primitive.indices];
			indexCount = indexAccessor.count;
			const tinygltf::BufferView& indexBufferView = gltfModel.bufferViews[indexAccessor.bufferView];
			indexBufferOffset = indexBufferView.byteOffset;

			switch (indexAccessor.componentType) {
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: // 5123 in the GLTF file
				indexType = VK_INDEX_TYPE_UINT16;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				indexType = VK_INDEX_TYPE_UINT32;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				indexType = VK_INDEX_TYPE_UINT8_EXT;
				break;
			default:
				return false;
			}

			//PrintLabeledDebugString("Index count: ", std::to_string(indexCount).c_str());
			//PrintLabeledDebugString("Index buffer view offset: ", std::to_string(indexBufferOffset).c_str());
			//PrintLabeledDebugString("Index type: ", std::to_string(indexAccessor.componentType).c_str());
		}

		// Create unified buffer for all data
		CreateGeometryBuffer(buffer.data.data(), buffer.data.size(), hasIndices);

		return true;
	}

	void CreateUniformBuffer() {
		VkDeviceSize bufferSize = sizeof(UniformBufferObject);

		// Create the uniform buffer
		GvkHelper::create_buffer(physicalDevice, device, bufferSize,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffer, &uniformBufferMemory);

		// Initialize camera matrices
		InitializeCameraMatrices();

		// Initial update of the uniform buffer
		UpdateUniformBuffer();
	}

	void InitializeCameraMatrices() {
		// Create view matrix using Gateware math (camera at pos 0,0,2 looking at origin)
		GW::MATH::GVECTORF eye = { -0.65f, 0.5f, -0.25f, 1.0f };
		GW::MATH::GVECTORF at = { 0.0f, 0.0f, 0.0f, 1.0f };
		GW::MATH::GVECTORF up = { 0.0f, 1.0f, 0.0f, 0.0f };

		// Store camera position for lighting calculations
		GW::MATH::GVECTORF cameraPosition = eye;

		// Use Gateware's matrix creation function
		GW::MATH::GMatrix::LookAtLHF(eye, at, up, viewMatrix);

		// Create projection matrix with 45 degree FOV
		float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
		GW::MATH::GMatrix::ProjectionVulkanLHF(
			G_DEGREE_TO_RADIAN(45), // 45 degrees in radians
			aspectRatio,            // Screen aspect ratio
			0.1f,                   // Near plane
			100.0f,                 // Far plane
			projectionMatrix        // Output matrix
		);

	}

	void UpdateUniformBuffer() {
		// Recalculate projection matrix if window was resized
		float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);

		// Update projection matrix for new aspect ratio
		GW::MATH::GMatrix::ProjectionVulkanLHF(
			G_DEGREE_TO_RADIAN(45),
			aspectRatio,
			0.1f,
			100.0f,
			projectionMatrix
		);

		// Extract camera position from the inverse view matrix
		GW::MATH::GMATRIXF inverseView;
		GW::MATH::GMatrix matrixMath;
		matrixMath.InverseF(viewMatrix, inverseView);

		// The camera position is in the last row of the inverse view matrix
		GW::MATH::GVECTORF cameraPosition = {
			inverseView.row4.x,
			inverseView.row4.y,
			inverseView.row4.z,
			1.0f
		};

		// Light direction +1x, -1y, -2z
		GW::MATH::GVECTORF lightDirection = { 2.0f, -2.0f, -1.0f, 0.0f };
		GW::MATH::GVector::NormalizeF(lightDirection, lightDirection);

		// Light color (white)
		GW::MATH::GVECTORF lightColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		// Prepare data to upload
		UniformBufferObject ubo;
		ubo.viewMatrix = viewMatrix;
		ubo.projectionMatrix = projectionMatrix;
		ubo.lightDirection = lightDirection;
		ubo.lightColor = lightColor;
		ubo.cameraPosition = cameraPosition;

		// Copy data to the uniform buffer
		void* data;
		vkMapMemory(device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
		memcpy(data, &ubo, sizeof(ubo));
		vkUnmapMemory(device, uniformBufferMemory);
	}

	void CreateTextureDescriptorSetLayout() {
		// Create binding for the texture sampler array
		VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
		samplerLayoutBinding.binding = 0;
		samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLayoutBinding.descriptorCount = MAX_TEXTURE_DESCRIPTORS; // Now supporting an array of textures
		samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // Only fragment shader needs texture
		samplerLayoutBinding.pImmutableSamplers = nullptr;

		// Set up binding flags to allow partially bound descriptors
		VkDescriptorBindingFlagsEXT bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT;

		VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlagsInfo = {};
		bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
		bindingFlagsInfo.bindingCount = 1;
		bindingFlagsInfo.pBindingFlags = &bindingFlags;

		// Create the descriptor set layout
		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.pNext = &bindingFlagsInfo; // Link the binding flags info
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &samplerLayoutBinding;

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &textureSetLayout) != VK_SUCCESS) {
			PrintLabeledDebugString("Error: ", "Failed to create texture descriptor set layout");
		}
	}

	void CreateDescriptorSetLayout() {
		// Create bindings for our uniform buffer and texture sampler
		std::vector<VkDescriptorSetLayoutBinding> bindings(2);

		// Binding 0: Uniform buffer
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[0].pImmutableSamplers = nullptr;

		// Binding 1: Texture sampler
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[1].pImmutableSamplers = nullptr;

		// Create the descriptor set layout
		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
			PrintLabeledDebugString("Error: ", "Failed to create descriptor set layout");
		}
	}

	void CreateDescriptorPool() {
		// Configure the descriptor pool sizes
		std::array<VkDescriptorPoolSize, 2> poolSizes = {};

		// Uniform buffer
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 1;

		// Texture sampler - now supporting an array of textures
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = MAX_TEXTURE_DESCRIPTORS;

		// Create the descriptor pool
		VkDescriptorPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 2; // Still need two descriptor sets: one for UBO and one for texture array

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
			PrintLabeledDebugString("Error: ", "Failed to create descriptor pool");
		}
	}

	void CreateTextureDescriptorSet() {
		// Only create the texture descriptor set if we have a valid sampler
		if (textureSampler == nullptr) {
			return;
		}

		// Determine the number of descriptors we need for our textures
		uint32_t variableDescriptorCount = static_cast<uint32_t>(textures.size());

		if (variableDescriptorCount == 0) {
			return; // No textures to bind
		}

		// Create variable descriptor count allocation info
		VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variableDescriptorCountInfo = {};
		variableDescriptorCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
		variableDescriptorCountInfo.descriptorSetCount = 1;
		variableDescriptorCountInfo.pDescriptorCounts = &variableDescriptorCount;

		// Allocate the descriptor set from the pool
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = &variableDescriptorCountInfo; // Link variable count info to enable bindless array
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &textureSetLayout;

		if (vkAllocateDescriptorSets(device, &allocInfo, &textureDescriptorSet) != VK_SUCCESS) {
			PrintLabeledDebugString("Error: ", "Failed to allocate texture descriptor set");
			return;
		}

		// Create an array of image infos for all textures
		std::vector<VkDescriptorImageInfo> imageInfos(textures.size());

		// Configure all texture descriptor entries at once
		for (size_t i = 0; i < textures.size(); i++) {
			imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfos[i].imageView = textures[i].imageView;
			imageInfos[i].sampler = textureSampler;
		}

		// Update the descriptor set with all textures at once
		VkWriteDescriptorSet descriptorWrite = {};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = textureDescriptorSet;
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0; // Start at the beginning of the array
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrite.descriptorCount = static_cast<uint32_t>(textures.size()); // Update all textures at once
		descriptorWrite.pImageInfo = imageInfos.data();

		vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

		PrintLabeledDebugString("Texture descriptor set updated with all textures", "");
		PrintLabeledDebugString("Number of textures bound: ", std::to_string(textures.size()).c_str());
	}

	void CreateDescriptorSet() {
		// Allocate the descriptor set from the pool
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descriptorSetLayout;

		if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
			PrintLabeledDebugString("Error: ", "Failed to allocate descriptor set");
		}

		// Update the descriptor set to point to our uniform buffer
		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = uniformBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

		// Uniform buffer descriptor
		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = descriptorSet;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfo;

		// Only add texture if we have textures in our vector and a valid sampler
		if (!textures.empty() && textureSampler != nullptr) {
			// Get the first texture from our vector
			const TextureData& firstTexture = textures[0];

			// Configure the texture descriptor
			VkDescriptorImageInfo imageInfo = {};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = firstTexture.imageView;
			imageInfo.sampler = textureSampler;

			// Texture sampler descriptor
			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSet;
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].dstArrayElement = 0;
			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &imageInfo;

			// Update with both descriptors
			vkUpdateDescriptorSets(device, 2, descriptorWrites.data(), 0, nullptr);
		}
		else {
			// Only update the uniform buffer descriptor
			vkUpdateDescriptorSets(device, 1, descriptorWrites.data(), 0, nullptr);
		}
	}

	void GetHandlesFromSurface()
	{
		vlk.GetDevice((void**)&device);
		vlk.GetPhysicalDevice((void**)&physicalDevice);
		vlk.GetRenderPass((void**)&renderPass);
	}

	void InitializeVertexBuffer()
	{

		float verts[] =
		{
			0,   0.5f,
			0.5f, -0.5f,
			-0.5f, -0.5f
		};

		CreateVertexBuffer(&verts[0], sizeof(verts));
	}

	void CreateVertexBuffer(const void* data, unsigned int sizeInBytes)
	{
		GvkHelper::create_buffer(physicalDevice, device, sizeInBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vertexHandle, &vertexData);
		GvkHelper::write_to_buffer(device, vertexData, data, sizeInBytes); // Transfer triangle data to the vertex buffer. (staging would be prefered here)
	}

	void CompileShaders()
	{
		// Intialize runtime shader compiler HLSL -> SPIRV
		shaderc_compiler_t compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t options = CreateCompileOptions();

		CompileVertexShader(compiler, options);
		CompileFragmentShader(compiler, options);

		// Free runtime shader compiler resources
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);
	}

	shaderc_compile_options_t CreateCompileOptions()
	{
		shaderc_compile_options_t retval = shaderc_compile_options_initialize();
		shaderc_compile_options_set_source_language(retval, shaderc_source_language_hlsl);
		shaderc_compile_options_set_invert_y(retval, false);
#ifndef NDEBUG
		shaderc_compile_options_set_generate_debug_info(retval);
#endif
		return retval;
	}

	void CompileVertexShader(const shaderc_compiler_t& compiler, const shaderc_compile_options_t& options)
	{
		std::string vertexShaderSource = ReadFileIntoString("../VertexShader.hlsl");

		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, vertexShaderSource.c_str(), vertexShaderSource.length(),
			shaderc_vertex_shader, "main.vert", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Vertex Shader Errors: \n", shaderc_result_get_error_message(result));
			abort(); //Vertex shader failed to compile! 
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &vertexShader);

		shaderc_result_release(result); // done
	}

	void CompileFragmentShader(const shaderc_compiler_t& compiler, const shaderc_compile_options_t& options)
	{
		std::string fragmentShaderSource = ReadFileIntoString("../FragmentShader.hlsl");

		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, fragmentShaderSource.c_str(), fragmentShaderSource.length(),
			shaderc_fragment_shader, "main.frag", "main", options);

		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
		{
			PrintLabeledDebugString("Fragment Shader Errors: \n", shaderc_result_get_error_message(result));
			abort(); //Fragment shader failed to compile! 
			return;
		}

		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &fragmentShader);

		shaderc_result_release(result); // done
	}

	// Create Pipeline & Layout (Thanks Tiny!)
	void InitializeGraphicsPipeline()
	{
		VkPipelineShaderStageCreateInfo stage_create_info[2] = {};

		// Create Stage Info for Vertex Shader
		stage_create_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage_create_info[0].module = vertexShader;
		stage_create_info[0].pName = "main";

		// Create Stage Info for Fragment Shader
		stage_create_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stage_create_info[1].module = fragmentShader;
		stage_create_info[1].pName = "main";


		VkPipelineInputAssemblyStateCreateInfo assembly_create_info = CreateVkPipelineInputAssemblyStateCreateInfo();
		
		// Only create binding descriptions if model is loaded
		uint32_t bindingCount = 1; // Default to 1 for basic triangle
		VkVertexInputBindingDescription binding_descriptions[4]; // Max 4 bindings

		if (modelLoaded) {
			// For GLTF model, use 4 bindings
			bindingCount = 4;

			// Position binding (binding 0)
			binding_descriptions[0].binding = 0;
			binding_descriptions[0].stride = positionInfo.stride;
			binding_descriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			// Normal binding (binding 1)
			binding_descriptions[1].binding = 1;
			binding_descriptions[1].stride = normalInfo.stride;
			binding_descriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			// TexCoord binding (binding 2)
			binding_descriptions[2].binding = 2;
			binding_descriptions[2].stride = texCoordInfo.stride;
			binding_descriptions[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			// Tangent binding (binding 3)
			binding_descriptions[3].binding = 3;
			binding_descriptions[3].stride = tangentInfo.stride;
			binding_descriptions[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		}
		else {
			// Use a single binding for basic triangle
			binding_descriptions[0] = CreateVkVertexInputBindingDescription();
		}

		// Create appropriate attribute descriptions
		uint32_t attributeCount = 1; // Default for basic triangle
		VkVertexInputAttributeDescription attribute_descriptions[4]; // Max 4 attributes

		if (modelLoaded) {
			attributeCount = 4;

			// Position attribute (binding 0, location 0)
			attribute_descriptions[0].binding = 0;
			attribute_descriptions[0].location = 0;
			attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // Position is VEC3
			attribute_descriptions[0].offset = 0;

			// Normal attribute (binding 1, location 1)
			attribute_descriptions[1].binding = 1;
			attribute_descriptions[1].location = 1;
			attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; // Normal is VEC3
			attribute_descriptions[1].offset = 0;

			// TexCoord attribute (binding 2, location 2)
			attribute_descriptions[2].binding = 2;
			attribute_descriptions[2].location = 2;
			attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT; // TexCoord is VEC2
			attribute_descriptions[2].offset = 0;

			// Tangent attribute (binding 3, location 3)
			attribute_descriptions[3].binding = 3;
			attribute_descriptions[3].location = 3;
			attribute_descriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT; // Tangent is VEC4
			attribute_descriptions[3].offset = 0;
		}
		else {
			// Single attribute for basic triangle
			attribute_descriptions[0].binding = 0;
			attribute_descriptions[0].location = 0;
			attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT; // 2D position for default triangle
			attribute_descriptions[0].offset = 0;
		}

		VkPipelineVertexInputStateCreateInfo input_vertex_info = CreateVkPipelineVertexInputStateCreateInfo(
			binding_descriptions, bindingCount,
			attribute_descriptions, attributeCount);

		VkVertexInputAttributeDescription vertex_attribute_descriptions[1];
		vertex_attribute_descriptions[0].binding = 0;
		vertex_attribute_descriptions[0].location = 0;

		// For GLTF models, use format based on the vertex stride
		if (modelLoaded && vertexStride == sizeof(float) * 3) 
		{
			vertex_attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // For 3D vectors
		}
		else 
		{
			vertex_attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;    // For 2D vectors
		}

		vertex_attribute_descriptions[0].offset = 0;

		VkViewport viewport = CreateViewportFromWindowDimensions();
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		VkPipelineViewportStateCreateInfo viewport_create_info = CreateVkPipelineViewportStateCreateInfo(&viewport, 1, &scissor, 1);
		VkPipelineRasterizationStateCreateInfo rasterization_create_info = CreateVkPipelineRasterizationStateCreateInfo();
		VkPipelineMultisampleStateCreateInfo multisample_create_info = CreateVkPipelineMultisampleStateCreateInfo();
		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = CreateVkPipelineDepthStencilStateCreateInfo();
		VkPipelineColorBlendAttachmentState color_blend_attachment_state = CreateVkPipelineColorBlendAttachmentState();
		VkPipelineColorBlendStateCreateInfo color_blend_create_info = CreateVkPipelineColorBlendStateCreateInfo(&color_blend_attachment_state, 1);

		VkDynamicState dynamic_states[2] =
		{
			// By setting these we do not need to re-create the pipeline on Resize
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamic_create_info = CreateVkPipelineDynamicStateCreateInfo(dynamic_states, 2);

		CreatePipelineLayout();

		// Pipeline State... (FINALLY) 
		VkGraphicsPipelineCreateInfo pipeline_create_info = {};
		pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = stage_create_info;
		pipeline_create_info.pInputAssemblyState = &assembly_create_info;
		pipeline_create_info.pVertexInputState = &input_vertex_info;
		pipeline_create_info.pViewportState = &viewport_create_info;
		pipeline_create_info.pRasterizationState = &rasterization_create_info;
		pipeline_create_info.pMultisampleState = &multisample_create_info;
		pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
		pipeline_create_info.pColorBlendState = &color_blend_create_info;
		pipeline_create_info.pDynamicState = &dynamic_create_info;
		pipeline_create_info.layout = pipelineLayout;
		pipeline_create_info.renderPass = renderPass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;

		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline);
	}

	VkPipelineShaderStageCreateInfo CreateVertexShaderStageCreateInfo()
	{
		VkPipelineShaderStageCreateInfo retval;
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		retval.stage = VK_SHADER_STAGE_VERTEX_BIT;
		retval.module = vertexShader;
		retval.pName = "main";
		return retval;
	}

	VkPipelineInputAssemblyStateCreateInfo CreateVkPipelineInputAssemblyStateCreateInfo()
	{
		VkPipelineInputAssemblyStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		retval.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		retval.primitiveRestartEnable = false;
		return retval;
	}

	VkVertexInputBindingDescription CreateVkVertexInputBindingDescription()
	{
		VkVertexInputBindingDescription retval = {};
		retval.binding = 0;
		retval.stride = vertexStride > 0 ? vertexStride : sizeof(float) * 2;
		retval.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return retval;
	}

	VkPipelineVertexInputStateCreateInfo CreateVkPipelineVertexInputStateCreateInfo(
		VkVertexInputBindingDescription* inputBindingDescriptions, unsigned int bindingCount,
		VkVertexInputAttributeDescription* vertexAttributeDescriptions, unsigned int attributeCount)
	{
		VkPipelineVertexInputStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		retval.vertexBindingDescriptionCount = bindingCount;
		retval.pVertexBindingDescriptions = inputBindingDescriptions;
		retval.vertexAttributeDescriptionCount = attributeCount;
		retval.pVertexAttributeDescriptions = vertexAttributeDescriptions;
		return retval;
	}

	VkViewport CreateViewportFromWindowDimensions()
	{
		VkViewport retval = {};
		retval.x = 0;
		retval.y = 0;
		retval.width = static_cast<float>(windowWidth);
		retval.height = static_cast<float>(windowHeight);
		retval.minDepth = 0;
		retval.maxDepth = 1;
		return retval;
	}

	VkRect2D CreateScissorFromWindowDimensions()
	{
		VkRect2D retval = {};
		retval.offset.x = 0;
		retval.offset.y = 0;
		retval.extent.width = windowWidth;
		retval.extent.height = windowHeight;
		return retval;
	}

	VkPipelineViewportStateCreateInfo CreateVkPipelineViewportStateCreateInfo(VkViewport* viewports, unsigned int viewportCount, VkRect2D* scissors, unsigned int scissorCount)
	{
		VkPipelineViewportStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		retval.viewportCount = viewportCount;
		retval.pViewports = viewports;
		retval.scissorCount = scissorCount;
		retval.pScissors = scissors;
		return retval;
	}

	VkPipelineRasterizationStateCreateInfo CreateVkPipelineRasterizationStateCreateInfo()
	{
		VkPipelineRasterizationStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		retval.rasterizerDiscardEnable = VK_FALSE;
		retval.polygonMode = VK_POLYGON_MODE_FILL;
		retval.lineWidth = 1.0f;
		retval.cullMode = VK_CULL_MODE_BACK_BIT;
		retval.frontFace = VK_FRONT_FACE_CLOCKWISE;
		retval.depthClampEnable = VK_FALSE;
		retval.depthBiasEnable = VK_FALSE;
		retval.depthBiasClamp = 0.0f;
		retval.depthBiasConstantFactor = 0.0f;
		retval.depthBiasSlopeFactor = 0.0f;
		return retval;
	}

	VkPipelineMultisampleStateCreateInfo CreateVkPipelineMultisampleStateCreateInfo()
	{
		VkPipelineMultisampleStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		retval.sampleShadingEnable = VK_FALSE;
		retval.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		retval.minSampleShading = 1.0f;
		retval.pSampleMask = VK_NULL_HANDLE;
		retval.alphaToCoverageEnable = VK_FALSE;
		retval.alphaToOneEnable = VK_FALSE;
		return retval;
	}

	VkPipelineDepthStencilStateCreateInfo CreateVkPipelineDepthStencilStateCreateInfo()
	{
		VkPipelineDepthStencilStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		retval.depthTestEnable = VK_TRUE;
		retval.depthWriteEnable = VK_TRUE;
		retval.depthCompareOp = VK_COMPARE_OP_LESS;
		retval.depthBoundsTestEnable = VK_FALSE;
		retval.minDepthBounds = 0.0f;
		retval.maxDepthBounds = 1.0f;
		retval.stencilTestEnable = VK_FALSE;
		return retval;
	}

	VkPipelineColorBlendAttachmentState CreateVkPipelineColorBlendAttachmentState()
	{
		VkPipelineColorBlendAttachmentState retval = {};
		retval.colorWriteMask = 0xF;
		retval.blendEnable = VK_FALSE;
		retval.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		retval.dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		retval.colorBlendOp = VK_BLEND_OP_ADD;
		retval.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		retval.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		retval.alphaBlendOp = VK_BLEND_OP_ADD;
		return retval;
	}

	VkPipelineColorBlendStateCreateInfo CreateVkPipelineColorBlendStateCreateInfo(VkPipelineColorBlendAttachmentState* attachments, unsigned int attachmentCount)
	{
		VkPipelineColorBlendStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		retval.logicOpEnable = VK_FALSE;
		retval.logicOp = VK_LOGIC_OP_COPY;
		retval.attachmentCount = attachmentCount;
		retval.pAttachments = attachments;
		retval.blendConstants[0] = 0.0f;
		retval.blendConstants[1] = 0.0f;
		retval.blendConstants[2] = 0.0f;
		retval.blendConstants[3] = 0.0f;
		return retval;
	}

	VkPipelineDynamicStateCreateInfo CreateVkPipelineDynamicStateCreateInfo(VkDynamicState* dynamicStates, unsigned int dynamicStateCount)
	{
		VkPipelineDynamicStateCreateInfo retval = {};
		retval.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		retval.dynamicStateCount = dynamicStateCount;
		retval.pDynamicStates = dynamicStates;
		return retval;
	}

	void CreatePipelineLayout() {
		// Create array of descriptor set layouts
		std::array<VkDescriptorSetLayout, 2> setLayouts = {
			descriptorSetLayout,  // Set 0: Uniform buffer
			textureSetLayout      // Set 1: Texture sampler
		};

		// Create the pipeline layout with both descriptor set layouts
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = hasTexture ? 2 : 1; // Use both layouts if we have a texture
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;

		if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
			PrintLabeledDebugString("Error: ", "Failed to create pipeline layout");
		}
	}

	void BindShutdownCallback()
	{
		// GVulkanSurface will inform us when to release any allocated resources
		shutdown.Create(vlk, [&]() {
			if (+shutdown.Find(GW::GRAPHICS::GVulkanSurface::Events::RELEASE_RESOURCES, true)) {
				CleanUp(); // unlike D3D we must be careful about destroy timing
			}
			});
	}


public:
	void Render()
	{
		// Calculate delta time (in a real application, this would come from a timer)
		static float lastFrameTime = 0.0f;
		float currentTime = static_cast<float>(GetTickCount64()) / 1000.0f;
		float deltaTime = currentTime - lastFrameTime;
		lastFrameTime = currentTime;

		// Update the view matrix
		UpdateViewMatrix(deltaTime);

		// Update uniform buffer with current camera data
		UpdateUniformBuffer();

		VkCommandBuffer commandBuffer = GetCurrentCommandBuffer();

		SetUpPipeline(commandBuffer);

		// Bind both descriptor sets if we have a texture
		if (hasTexture && textureDescriptorSet != nullptr) {
			// Bind both descriptor sets (UBO and texture)
			std::array<VkDescriptorSet, 2> descriptorSets = {
				descriptorSet,        // Set 0: Uniform buffer 
				textureDescriptorSet  // Set 1: Texture
			};

			vkCmdBindDescriptorSets(
				commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout,
				0, 2, descriptorSets.data(),
				0, nullptr
			);
		}
		else {
			// Just bind the uniform buffer descriptor set
			vkCmdBindDescriptorSets(
				commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout,
				0, 1, &descriptorSet,
				0, nullptr
			);
		}

		if (modelLoaded) {
			// We need to bind all four vertex attributes with their correct offsets
			const uint32_t bindingCount = 4; // Position, Normal, TexCoord, Tangent
			VkBuffer vertexBuffers[bindingCount] = {
				geometryBuffer, geometryBuffer, geometryBuffer, geometryBuffer
			};

			// Use the actual attribute offsets from the GLTF file
			VkDeviceSize offsets[bindingCount] = {
				positionInfo.byteOffset,
				normalInfo.byteOffset,
				texCoordInfo.byteOffset,
				tangentInfo.byteOffset
			};

			// Bind all vertex buffers at once with their corresponding offsets
			vkCmdBindVertexBuffers(commandBuffer, 0, bindingCount, vertexBuffers, offsets);

			// If we have indices, bind the index buffer and draw indexed
			if (hasIndexBuffer && indexCount > 0) {
				vkCmdBindIndexBuffer(commandBuffer, geometryBuffer, indexBufferOffset, indexType);
				vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
			}
			else {
				vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
			}
		}
		else {
			// Fall back to original triangle rendering with a single vertex attribute
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexHandle, offsets);
			vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		} 
	}

private:
	VkCommandBuffer GetCurrentCommandBuffer()
	{
		unsigned int currentBuffer;
		vlk.GetSwapchainCurrentImage(currentBuffer);

		VkCommandBuffer commandBuffer;
		vlk.GetCommandBuffer(currentBuffer, (void**)&commandBuffer);
		return commandBuffer;
	}

	void SetUpPipeline(VkCommandBuffer& commandBuffer)
	{
		UpdateWindowDimensions();
		SetViewport(commandBuffer);
		SetScissor(commandBuffer);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	}

	void SetViewport(const VkCommandBuffer& commandBuffer)
	{
		VkViewport viewport = CreateViewportFromWindowDimensions();
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	}

	void SetScissor(const VkCommandBuffer& commandBuffer)
	{
		VkRect2D scissor = CreateScissorFromWindowDimensions();
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	void BindVertexBuffers(VkCommandBuffer& commandBuffer)
	{
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexHandle, offsets);
	}


	//Cleanup callback function (passed to VKSurface, will be called when the pipeline shuts down)
	void CleanUp()
	{
		// wait till everything has completed
		vkDeviceWaitIdle(device);

		// Clean up texture resources
		if (textureSampler != nullptr) {
			vkDestroySampler(device, textureSampler, nullptr);
		}

		// Clean up all textures in our vector
		for (auto& texture : textures) {
			if (texture.imageView != nullptr) {
				vkDestroyImageView(device, texture.imageView, nullptr);
				texture.imageView = nullptr;
			}

			if (texture.image != nullptr) {
				vkDestroyImage(device, texture.image, nullptr);
				texture.image = nullptr;
			}

			if (texture.memory != nullptr) {
				vkFreeMemory(device, texture.memory, nullptr);
				texture.memory = nullptr;
			}

			if (texture.buffer != nullptr) {
				vkDestroyBuffer(device, texture.buffer, nullptr);
				texture.buffer = nullptr;
			}
		}

		textures.clear();

		// Clean up descriptor set layouts
		if (textureSetLayout != nullptr) {
			vkDestroyDescriptorSetLayout(device, textureSetLayout, nullptr);
		}

		// Clean up uniform buffer and descriptor resources
		if (uniformBuffer != nullptr) {
			vkDestroyBuffer(device, uniformBuffer, nullptr);
		}

		if (uniformBufferMemory != nullptr) {
			vkFreeMemory(device, uniformBufferMemory, nullptr);
		}

		if (descriptorPool != nullptr) {
			vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		}

		if (descriptorSetLayout != nullptr) {
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		}

		// Release allocated buffers, shaders & pipeline
		if (geometryBuffer != nullptr) {
			vkDestroyBuffer(device, geometryBuffer, nullptr);
		}

		if (geometryData != nullptr) {
			vkFreeMemory(device, geometryData, nullptr);
		}

		// Original buffer cleanup (can be removed if completely replaced)
		if (vertexHandle != nullptr) {
			vkDestroyBuffer(device, vertexHandle, nullptr);
		}

		if (vertexData != nullptr) {
			vkFreeMemory(device, vertexData, nullptr);
		}

		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, fragmentShader, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
	}
};
