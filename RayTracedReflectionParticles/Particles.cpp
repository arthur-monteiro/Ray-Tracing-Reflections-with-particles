#include "Particles.h"
#include <glm/gtx/matrix_decompose.hpp>

using namespace Wolf;

Particles::Particles(Wolf::WolfInstance* engineInstance, Wolf::Scene* scene, Wolf::Image* depth, Wolf::Image* color)
{
	m_particlesInfoBuffer = engineInstance->createBuffer(MAX_PARTICLE_COUNT * sizeof(ParticleInfo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_particlesVertexBuffer = engineInstance->createBuffer(MAX_PARTICLE_COUNT * sizeof(Vertex3DTextured) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_particlesIndexBuffer = engineInstance->createBuffer(MAX_PARTICLE_COUNT * sizeof(uint32_t) * 6, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	m_frameUniformBuffer = engineInstance->createUniformBufferObject(&m_frameUniformBufferData, sizeof(m_frameUniformBufferData));

	Scene::ComputePassCreateInfo computePassCreateInfo{};

	Scene::CommandBufferCreateInfo addCommandBufferInfo{};
	addCommandBufferInfo.commandType = Scene::CommandType::COMPUTE;
	addCommandBufferInfo.finalPipelineStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	m_updateCommandBuffer = scene->addCommandBuffer(addCommandBufferInfo);
	computePassCreateInfo.commandBufferID = m_updateCommandBuffer;

	computePassCreateInfo.computeShaderPath = "Shaders/particles/particleUpdate.spv";
	computePassCreateInfo.extent = { MAX_PARTICLE_COUNT, 1 };
	computePassCreateInfo.dispatchGroups = { 16, 1, 1 };

	DescriptorSetGenerator descriptorSetGenerator;
	descriptorSetGenerator.addBuffer(m_particlesInfoBuffer, VK_SHADER_STAGE_COMPUTE_BIT, 0);
	descriptorSetGenerator.addBuffer(m_particlesVertexBuffer, VK_SHADER_STAGE_COMPUTE_BIT, 1);
	descriptorSetGenerator.addUniformBuffer(m_frameUniformBuffer, VK_SHADER_STAGE_COMPUTE_BIT, 2);
	computePassCreateInfo.descriptorSetCreateInfo = descriptorSetGenerator.getDescritorSetCreateInfo();

	m_fullStart = std::chrono::high_resolution_clock::now();
	m_frameStart = std::chrono::high_resolution_clock::now();

	// Rendering
	m_renderUniformBuffer = engineInstance->createUniformBufferObject(&m_renderUniformBufferData, sizeof(m_renderUniformBufferData));

	std::vector<Attachment> debugPassAttachments(2);
	debugPassAttachments[0] = Attachment(engineInstance->getWindowSize(), depth->getFormat(), VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_GENERAL,
		VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, depth);
	debugPassAttachments[0].loadOperation = VK_ATTACHMENT_LOAD_OP_LOAD;
	debugPassAttachments[1] = Attachment(engineInstance->getWindowSize(), color->getFormat(), VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_GENERAL,
		VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT, color);
	debugPassAttachments[1].loadOperation = VK_ATTACHMENT_LOAD_OP_LOAD;

	Scene::CommandBufferCreateInfo renderingCommandBufferCreateInfo;
	renderingCommandBufferCreateInfo.finalPipelineStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	renderingCommandBufferCreateInfo.commandType = Scene::CommandType::GRAPHICS;
	m_renderingCommandBufferID = scene->addCommandBuffer(renderingCommandBufferCreateInfo);

	Scene::RenderPassCreateInfo renderingRenderPassCreateInfo{};
	renderingRenderPassCreateInfo.name = "Particles render pass";
	renderingRenderPassCreateInfo.commandBufferID = m_renderingCommandBufferID;
	renderingRenderPassCreateInfo.outputIsSwapChain = false;

	m_color = color;
	m_depth = depth;

	renderingRenderPassCreateInfo.beforeRecord = [](void* data, VkCommandBuffer commandBuffer) -> void
	{
		Particles* thisPtr = reinterpret_cast<Particles*>(data);
		Image::transitionImageLayoutUsingCommandBuffer(commandBuffer, thisPtr->m_color->getImage(), thisPtr->m_color->getFormat(), 
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0);
	};
	renderingRenderPassCreateInfo.dataForBeforeRecordCallback = this;

	renderingRenderPassCreateInfo.afterRecord = [](void* data, VkCommandBuffer commandBuffer) -> void
	{
		Particles* thisPtr = reinterpret_cast<Particles*>(data);
		Image::transitionImageLayoutUsingCommandBuffer(commandBuffer, thisPtr->m_color->getImage(), thisPtr->m_color->getFormat(),
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, 1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
	};
	renderingRenderPassCreateInfo.dataForAfterRecordCallback = this;

	std::vector<VkClearValue> renderPassClearValues;
	renderPassClearValues.resize(2);
	renderPassClearValues[0] = { -1.0f };
	renderPassClearValues[1] = { -1.0f, 0.0f, 0.0f, 1.0f };

	int i(0);
	for (auto& attachment : debugPassAttachments)
	{
		Scene::RenderPassOutput renderPassOutput;
		renderPassOutput.attachment = attachment;
		renderPassOutput.clearValue = renderPassClearValues[i++];

		renderingRenderPassCreateInfo.outputs.push_back(renderPassOutput);
	}

	m_renderPassID = scene->addRenderPass(renderingRenderPassCreateInfo);

	// Renderer creation (define the pipeline)
	RendererCreateInfo rendererCreateInfo{};
	rendererCreateInfo.renderPassID = m_renderPassID;
	//rendererCreateInfo.inputVerticesTemplate = InputVertexTemplate::POSITION_TEXTURECOORD_2D;
	rendererCreateInfo.inputVerticesTemplate = InputVertexTemplate::NO;
	rendererCreateInfo.instanceTemplate = InstanceTemplate::NO; // no instance template

	//std::vector<VkVertexInputAttributeDescription> inputAttributeDescriptions = ParticleInfo::getAttributeDescriptions(1, 2);
	//std::vector<VkVertexInputBindingDescription> inputBindingDescriptions = { ParticleInfo::getBindingDescription(1) };

// 	for (VkVertexInputAttributeDescription& inputAttributeDescription : inputAttributeDescriptions)
// 		rendererCreateInfo.pipelineCreateInfo.vertexInputAttributeDescriptions.push_back(inputAttributeDescription);
// 	for (VkVertexInputBindingDescription& inputBindingDescription : inputBindingDescriptions)
// 		rendererCreateInfo.pipelineCreateInfo.vertexInputBindingDescriptions.push_back(inputBindingDescription);

	rendererCreateInfo.pipelineCreateInfo.vertexInputAttributeDescriptions = Vertex3DTextured::getAttributeDescriptions(0);
	rendererCreateInfo.pipelineCreateInfo.vertexInputBindingDescriptions = { Vertex3DTextured::getBindingDescription(0) };

	ShaderCreateInfo vertexShaderCreateInfo{};
	vertexShaderCreateInfo.filename = "Shaders/particles/vert.spv";
	vertexShaderCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	rendererCreateInfo.pipelineCreateInfo.shaderCreateInfos.push_back(vertexShaderCreateInfo);

	ShaderCreateInfo fragmentShaderCreateInfo{};
	fragmentShaderCreateInfo.filename = "Shaders/particles/frag.spv";
	fragmentShaderCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	rendererCreateInfo.pipelineCreateInfo.shaderCreateInfos.push_back(fragmentShaderCreateInfo);

	rendererCreateInfo.pipelineCreateInfo.extent = engineInstance->getWindowSize();
	rendererCreateInfo.pipelineCreateInfo.alphaBlending = { true };

	m_particleTexture = engineInstance->createImageFromFile("Textures/circle_05.png");
	m_particleSampler = engineInstance->createSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, 1.0f, VK_FILTER_LINEAR);

	DescriptorSetGenerator renderingDescriptorSetGenerator{};
	renderingDescriptorSetGenerator.addUniformBuffer(m_renderUniformBuffer, VK_SHADER_STAGE_VERTEX_BIT, 0);
	renderingDescriptorSetGenerator.addCombinedImageSampler(m_particleTexture, m_particleSampler, VK_SHADER_STAGE_FRAGMENT_BIT, 1);

	rendererCreateInfo.descriptorLayouts = renderingDescriptorSetGenerator.getDescriptorLayouts();

	int rendererID = scene->addRenderer(rendererCreateInfo);

	// Create particle model
// 	std::vector<Vertex2DTextured> vertices =
// 	{
// 		{ glm::vec2(-1.0f, -1.0f), glm::vec2(0.0, 1.0) }, // top left
// 		{ glm::vec2(1.0f, -1.0f), glm::vec2(1.0, 1.0) }, // top right
// 		{ glm::vec2(-1.0f, 1.0f), glm::vec2(0.0, 0.0) }, // bot left
// 		{ glm::vec2(1.0f, 1.0f), glm::vec2(1.0, 0.0) } // bot right
// 	};
// 
// 	std::vector<uint32_t> indices =
// 	{
// 		0, 1, 2,
// 		1, 3, 2
// 	};
// 
// 	Model::ModelCreateInfo modelCreateInfo{};
// 	modelCreateInfo.inputVertexTemplate = InputVertexTemplate::POSITION_TEXTURECOORD_2D;
// 	m_particleModel = engineInstance->createModel(modelCreateInfo);
// 	m_particleModel->addMeshFromVertices(vertices.data(), vertices.size(), sizeof(Vertex2D), indices); // data are pushed to GPU here

	//m_instanceBuffer = engineInstance->createInstanceBuffer<ParticleInfo>();
	//m_instanceBuffer->createFromBuffer(m_particlesInfoBuffer);

	for (int i = 0; i < MAX_PARTICLE_COUNT; ++i)
	{
		m_indices.push_back(4 * i);
		m_indices.push_back(4 * i + 1);
		m_indices.push_back(4 * i + 2);

		m_indices.push_back(4 * i + 1);
		m_indices.push_back(4 * i + 3);
		m_indices.push_back(4 * i + 2);
	}

	Wolf::Buffer* particleIndexStagingBuffer = engineInstance->createBuffer(m_indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	void* data = nullptr;
	particleIndexStagingBuffer->map(&data);
	memcpy(data, m_indices.data(), m_indices.size() * sizeof(uint32_t));
	particleIndexStagingBuffer->unmap();

	m_particlesIndexBuffer->copy(particleIndexStagingBuffer);

	// Link the model to the renderer
	Renderer::AddMeshInfo addMeshInfo{};
	addMeshInfo.renderPassID = m_renderPassID;
	addMeshInfo.rendererID = rendererID;

	auto vertexBuffer = getWolfVertexBuffer();
	addMeshInfo.vertexBuffer = vertexBuffer;
	//addMeshInfo.vertexBuffer = m_particleModel->getVertexBuffers()[0];
	//addMeshInfo.instanceBuffer = { m_particlesInfoBuffer->getBuffer(), MAX_PARTICLE_COUNT };
	addMeshInfo.descriptorSetCreateInfo = renderingDescriptorSetGenerator.getDescritorSetCreateInfo();

	scene->addMesh(addMeshInfo);

	// Create acceleration structure
	{
		BottomLevelAccelerationStructure::GeometryInfo geometryInfo;
		geometryInfo.vertexBuffer = vertexBuffer;
		geometryInfo.vertexSize = sizeof(Vertex3DTextured);
		geometryInfo.transform = glm::mat4(1.0f);
		geometryInfo.transformBuffer = VK_NULL_HANDLE;
		geometryInfo.transformOffsetInBytes = 0;
		geometryInfo.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;

		m_accelerationStructure = engineInstance->createAccelerationStructure({ geometryInfo }, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR);

		computePassCreateInfo.afterRecord = [](void* data, VkCommandBuffer commandBuffer) -> void
		{
			Particles* thisPtr = reinterpret_cast<Particles*>(data);

			BottomLevelAccelerationStructure::GeometryInfo geometryInfo;
			geometryInfo.vertexBuffer = thisPtr->getWolfVertexBuffer();
			geometryInfo.vertexSize = sizeof(Vertex3DTextured);
			geometryInfo.transform = glm::mat4(1.0f);
			geometryInfo.transformBuffer = VK_NULL_HANDLE;
			geometryInfo.transformOffsetInBytes = 0;
			geometryInfo.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;

			thisPtr->m_accelerationStructure->rebuildBottomLevelAccelerationStructure(0, { geometryInfo }, commandBuffer);
		};
		computePassCreateInfo.dataForAfterRecordCallback = this;
	}

	m_computePassID = scene->addComputePass(computePassCreateInfo);
}

void Particles::update(glm::mat4 view, glm::mat4 projection)
{
	m_frameUniformBufferData.fullTime = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - m_fullStart).count()) / 2000.0f;
	m_frameUniformBufferData.frameTime = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - m_frameStart).count()) / 2000.0f;
	m_frameUniformBuffer->updateData(&m_frameUniformBufferData);
	m_frameStart = std::chrono::high_resolution_clock::now();
	
	m_renderUniformBufferData.view = view;
	m_renderUniformBufferData.invViewRot = glm::inverse(glm::mat3(view));
	m_renderUniformBufferData.projection = projection;
	m_renderUniformBuffer->updateData(&m_renderUniformBufferData);
}

Wolf::VertexBuffer Particles::getWolfVertexBuffer()
{
	VertexBuffer vertexBuffer;
	vertexBuffer.indexBuffer = m_particlesIndexBuffer->getBuffer();
	vertexBuffer.nbIndices = static_cast<int>(m_indices.size());
	vertexBuffer.vertexBuffer = m_particlesVertexBuffer->getBuffer();
	vertexBuffer.nbVertices = 4 * MAX_PARTICLE_COUNT;

	return vertexBuffer;
}
