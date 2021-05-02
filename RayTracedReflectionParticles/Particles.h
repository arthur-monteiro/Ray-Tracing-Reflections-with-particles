#pragma once

#include <WolfEngine.h>

#define MAX_PARTICLE_COUNT 10'000

class Particles
{
public:
	Particles(Wolf::WolfInstance* engineInstance, Wolf::Scene* scene, Wolf::Image* depth, Wolf::Image* color);

	void update(glm::mat4 view, glm::mat4 projection);

	int getUpdateCommandBufferID() { return m_updateCommandBuffer; }
	int getRenderingCommandBufferID() { return m_renderingCommandBufferID; }

	Wolf::AccelerationStructure* getAccelerationStructure() const { return m_accelerationStructure;	}
	VkBuffer getVertexBuffer() { return m_particlesVertexBuffer->getBuffer(); }
	VkBuffer getIndexBuffer() { return m_particlesIndexBuffer->getBuffer(); }
	Wolf::Sampler* getSampler() { return m_particleSampler; }
	Wolf::Image* getTexture() { return m_particleTexture; }

private:
	Wolf::VertexBuffer getWolfVertexBuffer();

private:
	struct ParticleInfo
	{
		glm::vec3 position;
		float bornTime;
		glm::vec3 velocity;
		float lifeTime;

		static VkVertexInputBindingDescription getBindingDescription(uint32_t binding)
		{
			VkVertexInputBindingDescription bindingDescription = {};
			bindingDescription.binding = binding;
			bindingDescription.stride = sizeof(ParticleInfo);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

			return bindingDescription;
		}

		static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions(uint32_t binding, uint32_t startLocation)
		{
			std::vector<VkVertexInputAttributeDescription> attributeDescriptions(2);

			attributeDescriptions[0].binding = binding;
			attributeDescriptions[0].location = startLocation;
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[0].offset = 0;

			attributeDescriptions[1].binding = binding;
			attributeDescriptions[1].location = startLocation + 1;
			attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[1].offset = 0;

			return attributeDescriptions;
		}
	};
	Wolf::Buffer* m_particlesInfoBuffer;

	struct Vertex3DTextured
	{
		glm::vec3 pos;
		glm::vec2 texCoord;

		static VkVertexInputBindingDescription getBindingDescription(uint32_t binding)
		{
			VkVertexInputBindingDescription bindingDescription = {};
			bindingDescription.binding = binding;
			bindingDescription.stride = sizeof(Vertex3DTextured);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			return bindingDescription;
		}

		static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions(uint32_t binding)
		{
			std::vector<VkVertexInputAttributeDescription> attributeDescriptions(2);

			attributeDescriptions[0].binding = binding;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(Vertex3DTextured, pos);

			attributeDescriptions[1].binding = binding;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescriptions[1].offset = offsetof(Vertex3DTextured, texCoord);

			return attributeDescriptions;
		}

		bool operator==(const Vertex3DTextured& other) const
		{
			return pos == other.pos && texCoord == other.texCoord;
		}
	};
	Wolf::Buffer* m_particlesVertexBuffer;

	std::vector<uint32_t> m_indices;
	Wolf::Buffer* m_particlesIndexBuffer;

	struct FrameUniformBuffer
	{
		float frameTime = 0.0f;
		float fullTime = 0.0f;
	};
	FrameUniformBuffer m_frameUniformBufferData;
	Wolf::UniformBuffer* m_frameUniformBuffer;

	int m_computePassID;
	int m_updateCommandBuffer;

	std::chrono::steady_clock::time_point m_fullStart;
	std::chrono::steady_clock::time_point m_frameStart;

	// Rendering
	struct RenderUniformBuffer
	{
		glm::mat4 view;
		glm::mat4 projection;
		glm::mat4 invViewRot;
	};
	RenderUniformBuffer m_renderUniformBufferData;
	Wolf::UniformBuffer* m_renderUniformBuffer;

	int m_renderingCommandBufferID;

	Wolf::Image* m_depth;
	Wolf::Image* m_color;

	int m_renderPassID;

	//Wolf::Model* m_particleModel;
	//Wolf::Instance<ParticleInfo>* m_instanceBuffer;
	Wolf::Image* m_particleTexture;
	Wolf::Sampler* m_particleSampler;

	Wolf::AccelerationStructure* m_accelerationStructure;
};

