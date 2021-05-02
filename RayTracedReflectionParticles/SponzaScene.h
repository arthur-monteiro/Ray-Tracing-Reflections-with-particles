#pragma once

#include <WolfEngine.h>
#include <Template3D.h>

#include "Camera.h"
#include "Particles.h"

class SponzaScene
{
public:
	SponzaScene(Wolf::WolfInstance* wolfInstance);

	void update();

	[[nodiscard]] Wolf::Scene* getScene() const { return m_scene; }
	[[nodiscard]] std::vector<int> getCommandBufferToSubmit();
	[[nodiscard]] std::vector<std::pair<int, int>> getCommandBufferSynchronisation();

private:
	Camera m_camera;
	GLFWwindow* m_window;

	Wolf::Scene* m_scene = nullptr;

	// Data
	glm::vec3 m_lightDir = glm::vec3(4.0f, -5.0f, -1.5f);
	Wolf::UniformBuffer* m_uboMVP;
	glm::mat4 m_projectionMatrix;
	glm::mat4 m_viewMatrix;
	glm::mat4 m_modelMatrix;

	// Effects
	int m_gBufferCommandBufferID = -2;
	std::unique_ptr<Wolf::GBuffer> m_GBuffer;

	std::unique_ptr<Wolf::CascadedShadowMapping> m_cascadedShadowMapping;

	Wolf::Texture* m_directLightingOutputTexture;
	Wolf::Image* m_reflectionQuantityImage;
	struct DirectLightingUBData
	{
		glm::mat4 invProjection;
		glm::mat4 invView;
		glm::mat4 voxelProjection;
		glm::vec4 projParams;
		glm::vec4 directionDirectionalLight;
		glm::vec4 colorDirectionalLight;
	};
	DirectLightingUBData m_directLightingUBData;
	Wolf::UniformBuffer* m_directLightingUniformBuffer;
	int m_directLightingCommandBufferID;

	std::unique_ptr<Particles> m_particles;

	// RT reflections
	Wolf::Sampler* m_rtSampler;
	struct RayGenUB
	{
		glm::mat4 invProjection;
		glm::mat4 invView;
		glm::vec4 projParams;
		glm::vec4 camPos;
	};
	RayGenUB m_rayGenUBData;
	Wolf::UniformBuffer* m_rayGenUB;

	struct ClosestHitUB
	{
		glm::mat4 model;
		glm::vec4 directionDirectionalLight;
		glm::vec4 colorDirectionalLight;
	};
	ClosestHitUB m_closestHitUBData;
	Wolf::UniformBuffer* m_closestHitUB;

	int m_reflectionCommandBuffer;
	Wolf::AccelerationStructure* m_accelerationStructure;
};

