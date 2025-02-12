
#include <VulkanObjects.h>

#include <cmath>
#include <memory>
#include <fstream>
#include <iostream>

int main(int, const char* [])
{
	try {
		THROW_ON_VULKAN_ERROR(volkInitialize());

		auto instance = std::make_shared<VulkanInstance>();

		auto physicalDevices = instance->getVulkanPhysicalDevices();
		if (physicalDevices.empty()) {
			throw Exception("No Vulkan Devices found!");
		}
		std::cout << std::format("Found {} Vulkan Device(s):", physicalDevices.size()) << std::endl;
		for (const auto& physicalDevice : physicalDevices) {
			std::cout << std::format("Device ID: {} Device Name: {} Driver version: {}",
				physicalDevice->deviceID(), physicalDevice->deviceName(), physicalDevice->driverVersion()) << std::endl;
		}

		auto physicalDevice = physicalDevices.front();
		auto graphicsQueueFamilyIndex = physicalDevice->getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_SPARSE_BINDING_BIT);
		auto graphicsQueueIndex = physicalDevice->addQueue(graphicsQueueFamilyIndex);

		auto device = std::make_shared<VulkanDevice>(instance, physicalDevice, physicalDevice->deviceQueueCreateInfos);
		auto graphicsQueue = std::make_shared<VulkanQueue>(device, graphicsQueueFamilyIndex, graphicsQueueIndex);
		auto fence = std::make_shared<VulkanFence>(device);

		VkExtent3D imageExtent{ .width = 16384, .height = 16384, .depth = 2048 };
		auto maxExtent = std::max(imageExtent.width, std::max(imageExtent.height, imageExtent.depth));
		auto numLevels = std::floor(std::log2(maxExtent)) + 1;

		auto image = std::make_shared<VulkanImage>(device,
			VulkanImage::Config{
				.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT,
				.imageType = VK_IMAGE_TYPE_3D,
				.format = VK_FORMAT_R8_SNORM,
				.extent = imageExtent,
				.mipLevels = static_cast<uint32_t>(numLevels),
				.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			});

		auto memoryRequirements = device->getMemoryRequirements(image->image);
		memoryRequirements.size = size_t(1) << 30; // 1 GiB
		auto memory = std::make_shared<VulkanMemory>(device, memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		auto tileExtent = VkExtent3D{ 64, 64, 64 };
		VkDeviceSize tileSize = VkDeviceSize(tileExtent.width) * VkDeviceSize(tileExtent.height) * VkDeviceSize(tileExtent.depth);

		const size_t batchSize = 16;
		std::vector<double> bindTimes;
		std::vector<VkSparseImageMemoryBind> sparseImageMemoryBinds;
		VkSparseImageMemoryBindInfo sparseImageMemoryBindInfo;

		const uint32_t num_tiles_i = 125;
		const uint32_t num_tiles_j = 125;
		const uint32_t numBinds = num_tiles_i * num_tiles_j;

		for (uint32_t bind = 0; bind < numBinds; bind++) {

			uint32_t i = bind / num_tiles_j;
			uint32_t j = bind % num_tiles_j;
			uint32_t k = 0;

			sparseImageMemoryBinds.emplace_back(
				VkSparseImageMemoryBind{
					.subresource = VkImageSubresource{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel = k,
						.arrayLayer = 0,
					},
					.offset = VkOffset3D{
						int32_t(i * tileExtent.width),
						int32_t(j * tileExtent.height),
						int32_t(k * tileExtent.depth),
					},
					.extent = tileExtent,
					.memory = memory->memory,
					.memoryOffset = (bind * tileSize) % memoryRequirements.size,
					.flags = 0,
				});

			if (sparseImageMemoryBinds.size() % batchSize == 0) {
				sparseImageMemoryBindInfo = {
					.image = image->image,
					.bindCount = static_cast<uint32_t>(sparseImageMemoryBinds.size()),
					.pBinds = sparseImageMemoryBinds.data(),
				};

				{
					Timer timer;
					graphicsQueue->bindSparse(sparseImageMemoryBindInfo, fence->fence);
					fence->waitAndReset();
					bindTimes.push_back(timer.getElapsedTimeSeconds());
				}

				sparseImageMemoryBinds.clear();
			}

			if (bind % (numBinds / 100) == 0) {
				float percent = float(bind) / numBinds;
				std::cout << "Collecting data... " << int(std::ceil(percent * 100)) << "%" << std::endl;
			}
		}

		std::ofstream outFile("bindTimes.txt");
		if (outFile.is_open()) {
			for (auto& bindTime : bindTimes) {
				outFile << bindTime << std::endl;
			}
		}
		return EXIT_SUCCESS;
	}
	catch (const std::exception& e) {
		std::cerr << e.what();
		return EXIT_FAILURE;
	}
}
