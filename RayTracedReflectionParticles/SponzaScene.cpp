#include "SponzaScene.h"

using namespace Wolf;

SponzaScene::SponzaScene(Wolf::WolfInstance* wolfInstance)
{
	m_window = wolfInstance->getWindowPtr();

	// Scene creation
	Scene::SceneCreateInfo sceneCreateInfo;
	sceneCreateInfo.swapChainCommandType = Scene::CommandType::COMPUTE;

	m_scene = wolfInstance->createScene(sceneCreateInfo);

	// Model creation
	Model::ModelCreateInfo modelCreateInfo{};
	modelCreateInfo.inputVertexTemplate = InputVertexTemplate::FULL_3D_MATERIAL;
	Model* model = wolfInstance->createModel<>(modelCreateInfo);

	Model::ModelLoadingInfo modelLoadingInfo;
	modelLoadingInfo.filename = std::move("Models/sponza/sponza.obj");
	modelLoadingInfo.mtlFolder = std::move("Models/sponza");
	model->loadObj(modelLoadingInfo);

	// Data
	float near = 0.1f;
	float far = 100.0f;
	{
		m_projectionMatrix = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, near, far);
		m_projectionMatrix[1][1] *= -1;
		m_viewMatrix = glm::lookAt(glm::vec3(-2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		m_modelMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
	}

	{
		// GBuffer
		Scene::CommandBufferCreateInfo commandBufferCreateInfo;
		commandBufferCreateInfo.finalPipelineStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		commandBufferCreateInfo.commandType = Scene::CommandType::GRAPHICS;
		m_gBufferCommandBufferID = m_scene->addCommandBuffer(commandBufferCreateInfo);

		m_GBuffer = std::make_unique<GBuffer>(wolfInstance, m_scene, m_gBufferCommandBufferID, wolfInstance->getWindowSize(), VK_SAMPLE_COUNT_1_BIT, model, glm::mat4(1.0f), true);

		Image* depth = m_GBuffer->getDepth();
		Image* albedo = m_GBuffer->getAlbedo();
		Image* normalRoughnessMetal = m_GBuffer->getNormalRoughnessMetal();

		// CSM
		m_cascadedShadowMapping = std::make_unique<CascadedShadowMapping>(wolfInstance, m_scene, model, 0.1f, 100.0f, 32.f, glm::radians(45.0f), wolfInstance->getWindowSize(),
			depth, m_projectionMatrix);

		// RTGI
		// Acceleration structure creation
		{
			BottomLevelAccelerationStructure::GeometryInfo geometryInfo;
			geometryInfo.vertexBuffer = model->getVertexBuffers()[0];
			geometryInfo.vertexSize = sizeof(Vertex3D);
			geometryInfo.transform = m_modelMatrix;
			geometryInfo.transformBuffer = VK_NULL_HANDLE;
			geometryInfo.transformOffsetInBytes = 0;
			geometryInfo.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;

			m_accelerationStructure = wolfInstance->createAccelerationStructure({ geometryInfo });
		}

		// Direct lighting
#pragma warning(disable: 4996)
		m_directLightingOutputTexture = wolfInstance->createTexture();
		m_directLightingOutputTexture->create({ wolfInstance->getWindowSize().width, wolfInstance->getWindowSize().height, 1 }, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		m_directLightingOutputTexture->setImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		m_reflectionQuantityImage = wolfInstance->createImage({ wolfInstance->getWindowSize().width, wolfInstance->getWindowSize().height, 1 }, VK_IMAGE_USAGE_STORAGE_BIT, 
			VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		m_directLightingUBData.directionDirectionalLight = glm::vec4(m_lightDir, 1.0f);
		m_directLightingUBData.colorDirectionalLight = glm::vec4(10.0f, 9.0f, 6.0f, 1.0f);
		m_directLightingUBData.invProjection = glm::inverse(m_projectionMatrix);
		m_directLightingUBData.invView = glm::inverse(m_viewMatrix);
		m_directLightingUBData.projParams.x = far / (far - near);
		m_directLightingUBData.projParams.y = (-far * near) / (far - near);
		m_directLightingUniformBuffer = wolfInstance->createUniformBufferObject(&m_directLightingUBData, sizeof(m_directLightingUBData));

		Scene::ComputePassCreateInfo directLigtingComputePassCreateInfo;
		directLigtingComputePassCreateInfo.extent = wolfInstance->getWindowSize();
		directLigtingComputePassCreateInfo.dispatchGroups = { 16, 16, 1 };
		directLigtingComputePassCreateInfo.computeShaderPath = "Shaders/directLighting/comp.spv";

		commandBufferCreateInfo.finalPipelineStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		commandBufferCreateInfo.commandType = Scene::CommandType::COMPUTE;
		m_directLightingCommandBufferID = m_scene->addCommandBuffer(commandBufferCreateInfo);
		directLigtingComputePassCreateInfo.commandBufferID = m_directLightingCommandBufferID;

		{
			DescriptorSetGenerator descriptorSetGenerator;
			descriptorSetGenerator.addImages({ depth }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0);
			descriptorSetGenerator.addImages({ albedo }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1);
			descriptorSetGenerator.addImages({ normalRoughnessMetal }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 2);
			descriptorSetGenerator.addImages({ m_cascadedShadowMapping->getOutputShadowMaskTexture()->getImage() }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 3);
			descriptorSetGenerator.addImages({ m_cascadedShadowMapping->getOutputVolumetricLightMaskTexture()->getImage() }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 4);
			descriptorSetGenerator.addImages({ m_reflectionQuantityImage }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 5);
			descriptorSetGenerator.addImages({ m_directLightingOutputTexture->getImage() }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 6);
			descriptorSetGenerator.addUniformBuffer(m_directLightingUniformBuffer, VK_SHADER_STAGE_COMPUTE_BIT, 7);

			directLigtingComputePassCreateInfo.descriptorSetCreateInfo = descriptorSetGenerator.getDescritorSetCreateInfo();
		}

		m_scene->addComputePass(directLigtingComputePassCreateInfo);

		// Particles
		m_particles = std::make_unique<Particles>(wolfInstance, m_scene, depth, m_directLightingOutputTexture->getImage());

		// Reflections
		{
			commandBufferCreateInfo.finalPipelineStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			commandBufferCreateInfo.commandType = Scene::CommandType::RAY_TRACING;
			m_reflectionCommandBuffer = m_scene->addCommandBuffer(commandBufferCreateInfo);

			Scene::RayTracingPassAddInfo rayTracingPassAddInfo;
			rayTracingPassAddInfo.extent = { wolfInstance->getWindowSize().width, wolfInstance->getWindowSize().height, 1 };
			rayTracingPassAddInfo.commandBufferID = m_reflectionCommandBuffer;
			rayTracingPassAddInfo.outputIsSwapChain = false;

			RayTracingPass::RayTracingPassCreateInfo rayTracingPassCreateInfo;
			rayTracingPassCreateInfo.raygenShader = "Shaders/reflections/rgen.spv";
			rayTracingPassCreateInfo.missShaders = { "Shaders/reflections/rmiss.spv", "Shaders/reflections/shadowRMiss.spv" };

			RayTracingPass::RayTracingPassCreateInfo::HitGroup hitGroup;
			hitGroup.closestHitShader = "Shaders/reflections/rchit.spv";

			RayTracingPass::RayTracingPassCreateInfo::HitGroup particleHitGroup;
			particleHitGroup.closestHitShader = "Shaders/reflections/particle_rchit.spv";

			rayTracingPassCreateInfo.hitGroups = { hitGroup, particleHitGroup };

			// Descriptor set
			DescriptorSetGenerator descriptorSetGenerator;
			descriptorSetGenerator.addAccelerationStructure(m_accelerationStructure, VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 0);
			descriptorSetGenerator.addAccelerationStructure(m_particles->getAccelerationStructure(), VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 1);
			descriptorSetGenerator.addImages({ m_reflectionQuantityImage }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_NV, 2);

			m_rayGenUBData.invProjection = glm::inverse(m_projectionMatrix);
			m_rayGenUBData.projParams = m_directLightingUBData.projParams;
			m_rayGenUB = wolfInstance->createUniformBufferObject(&m_rayGenUBData, sizeof(m_rayGenUBData));
			descriptorSetGenerator.addUniformBuffer(m_rayGenUB, VK_SHADER_STAGE_RAYGEN_BIT_NV, 3);

			descriptorSetGenerator.addBuffer(model->getVertexBuffers()[0].vertexBuffer, VK_WHOLE_SIZE, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 4);
			descriptorSetGenerator.addBuffer(model->getVertexBuffers()[0].indexBuffer, VK_WHOLE_SIZE, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 5);
			descriptorSetGenerator.addBuffer(m_particles->getVertexBuffer(), VK_WHOLE_SIZE, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 6);
			descriptorSetGenerator.addBuffer(m_particles->getIndexBuffer(), VK_WHOLE_SIZE, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 7);

			m_rtSampler = wolfInstance->createSampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, model->getImages()[0]->getMipLevels(), VK_FILTER_LINEAR, 16.0f,
				model->getImages()[0]->getMipLevels() / 3.0f);
			descriptorSetGenerator.addSampler(m_rtSampler, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 8);

			descriptorSetGenerator.addImages(model->getImages(), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 9);
			descriptorSetGenerator.addImages({ depth }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_NV, 10);
			descriptorSetGenerator.addImages({ normalRoughnessMetal }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_NV, 11);
			descriptorSetGenerator.addImages({ m_directLightingOutputTexture->getImage() }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_NV, 12);

			m_closestHitUBData.model = m_modelMatrix;
			m_closestHitUBData.directionDirectionalLight = m_directLightingUBData.directionDirectionalLight;
			m_closestHitUBData.colorDirectionalLight = m_directLightingUBData.colorDirectionalLight;
			m_closestHitUB = wolfInstance->createUniformBufferObject(&m_closestHitUBData, sizeof(m_closestHitUBData));
			descriptorSetGenerator.addUniformBuffer(m_closestHitUB, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 13);

			descriptorSetGenerator.addSampler(m_particles->getSampler(), VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 14);
			descriptorSetGenerator.addImages({ m_particles->getTexture() }, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, 15);

			rayTracingPassCreateInfo.descriptorSetCreateInfo = descriptorSetGenerator.getDescritorSetCreateInfo();
			rayTracingPassAddInfo.rayTracingPassCreateInfo = rayTracingPassCreateInfo;

			int rayTracingPassID = m_scene->addRayTracingPass(rayTracingPassAddInfo);
		}

		// Tone mapping
		Scene::ComputePassCreateInfo toneMappingComputePassCreateInfo;
		toneMappingComputePassCreateInfo.computeShaderPath = "Shaders/toneMapping/comp.spv";
		toneMappingComputePassCreateInfo.outputIsSwapChain = true;
		toneMappingComputePassCreateInfo.commandBufferID = -1;
		toneMappingComputePassCreateInfo.dispatchGroups = { 16, 16, 1 };

		DescriptorSetGenerator toneMappingDescriptorSetGenerator;
		toneMappingDescriptorSetGenerator.addImages({ m_directLightingOutputTexture->getImage() }, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0);

		toneMappingComputePassCreateInfo.descriptorSetCreateInfo = toneMappingDescriptorSetGenerator.getDescritorSetCreateInfo();
		toneMappingComputePassCreateInfo.outputBinding = 1;

		m_scene->addComputePass(toneMappingComputePassCreateInfo);
	}

	m_scene->record();

	m_camera.initialize(glm::vec3(1.4f, 1.2f, 0.3f), glm::vec3(2.0f, 0.9f, -0.3f), glm::vec3(0.0f, 1.0f, 0.0f), 0.01f, 5.0f,
		16.0f / 9.0f);
}

void SponzaScene::update()
{
	m_camera.update(m_window);
	m_viewMatrix = m_camera.getViewMatrix();

	m_GBuffer->updateMVPMatrix(m_modelMatrix, m_viewMatrix, m_projectionMatrix);

	m_cascadedShadowMapping->updateMatrices(m_lightDir, m_camera.getPosition(), m_camera.getOrientation(), m_modelMatrix, glm::inverse(m_viewMatrix * m_modelMatrix));

	m_directLightingUBData.invView = glm::inverse(m_viewMatrix);
	m_directLightingUBData.directionDirectionalLight = glm::transpose(m_directLightingUBData.invView) * glm::vec4(m_lightDir, 1.0f);
	m_directLightingUniformBuffer->updateData(&m_directLightingUBData);

	m_particles->update(m_viewMatrix, m_projectionMatrix);

	m_rayGenUBData.invView = m_directLightingUBData.invView;
	m_rayGenUBData.camPos = glm::vec4(m_camera.getPosition(), 0.0f);
	m_rayGenUB->updateData(&m_rayGenUBData);
}

std::vector<int> SponzaScene::getCommandBufferToSubmit()
{
	std::vector<int> r;
	r.push_back(m_gBufferCommandBufferID);
	std::vector<int> csmCommandBuffer = m_cascadedShadowMapping->getCascadeCommandBuffers();
	for (auto& commandBuffer : csmCommandBuffer)
		r.push_back(commandBuffer);
	r.push_back(m_directLightingCommandBufferID);
	r.push_back(m_particles->getUpdateCommandBufferID());
	r.push_back(m_particles->getRenderingCommandBufferID());
	r.push_back(m_reflectionCommandBuffer);

	return r;
}

std::vector<std::pair<int, int>> SponzaScene::getCommandBufferSynchronisation()
{
	std::vector<std::pair<int, int>> r =
	{ { m_gBufferCommandBufferID, m_directLightingCommandBufferID}, { m_directLightingCommandBufferID, m_particles->getRenderingCommandBufferID() } };

	std::vector<std::pair<int, int>> csmSynchronisation = m_cascadedShadowMapping->getCommandBufferSynchronisation();
	for (auto& sync : csmSynchronisation)
	{
		r.push_back(sync);
	}
	r.emplace_back(m_cascadedShadowMapping->getCascadeCommandBuffers().back(), m_directLightingCommandBufferID);
	r.emplace_back(m_particles->getUpdateCommandBufferID(), m_particles->getRenderingCommandBufferID());
	r.emplace_back(m_particles->getRenderingCommandBufferID(), m_reflectionCommandBuffer);
	r.emplace_back(m_reflectionCommandBuffer, -1);

	return r;
}