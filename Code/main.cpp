
#include <VulkanObjects.h>

#include <cmath>
#include <memory>
#include <fstream>
#include <iostream>
#include <filesystem>

int main(int, const char* [])
{
	try {
		THROW_ON_VULKAN_ERROR(volkInitialize());

		auto instance = std::make_shared<VulkanInstance>();

		auto physicalDevices = instance->getVulkanPhysicalDevices();
		if (physicalDevices.empty()) {
			throw Exception("No Vulkan Devices found!");
		}

		for (auto& physicalDevice : physicalDevices) {
			auto device_info = std::format("{}, Driver version: {}", physicalDevice->deviceName(), physicalDevice->driverVersion());
			std::cout << device_info << std::endl;

			auto graphicsQueueFamilyIndex = physicalDevice->getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_SPARSE_BINDING_BIT);
			auto graphicsQueueIndex = physicalDevice->addQueue(graphicsQueueFamilyIndex);

			auto device = std::make_shared<VulkanDevice>(instance, physicalDevice, physicalDevice->deviceQueueCreateInfos);
			auto graphicsQueue = std::make_shared<VulkanQueue>(device, graphicsQueueFamilyIndex, graphicsQueueIndex);
			auto fence = std::make_shared<VulkanFence>(device);

			VulkanImage::Config imageConfig{
				.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT,
				.imageType = VK_IMAGE_TYPE_3D,
				.format = VK_FORMAT_R8_SNORM,
				.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			};

			auto sparseAddressSpaceSize = physicalDevice->getSparseAddressSpaceSize();
			std::cout << std::format("Sparse address space: {} TiB",
				sparseAddressSpaceSize / double(1ULL << 40)) << std::endl;

			auto imageFormatProperties = physicalDevice->getPhysicalDeviceImageFormatProperties(
				imageConfig.format,
				imageConfig.imageType,
				imageConfig.tiling,
				imageConfig.usage,
				imageConfig.flags);

			std::cout << std::format(
				"Image max extent: ({}, {}, {})",
				imageFormatProperties.maxExtent.width,
				imageFormatProperties.maxExtent.height,
				imageFormatProperties.maxExtent.depth) << std::endl;

			VkExtent3D imageExtent{
				std::min(imageFormatProperties.maxExtent.width, 4096u),
				std::min(imageFormatProperties.maxExtent.height, 4096u),
				std::min(imageFormatProperties.maxExtent.depth, 1024u),
			};

			auto imageSize =
				static_cast<VkDeviceSize>(imageExtent.width) *
				static_cast<VkDeviceSize>(imageExtent.height) *
				static_cast<VkDeviceSize>(imageExtent.depth);

			if (imageSize > sparseAddressSpaceSize) {
				throw Exception("not enough sparse address space for image size.");
			}

			auto maxExtent = std::max(imageExtent.width, std::max(imageExtent.height, imageExtent.depth));
			auto numLevels = std::floor(std::log2(maxExtent)) + 1;
			imageConfig.extent = imageExtent;
			imageConfig.mipLevels = static_cast<uint32_t>(numLevels);

			auto image = std::make_shared<VulkanImage>(device, imageConfig);

			auto memoryRequirements = device->getMemoryRequirements(image->image);
			memoryRequirements.size = size_t(1) << 30; // 1 GiB
			auto memory = std::make_shared<VulkanMemory>(device, memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			auto tileExtent = VkExtent3D{ 64, 64, 64 };
			VkDeviceSize tileSize = VkDeviceSize(tileExtent.width) * VkDeviceSize(tileExtent.height) * VkDeviceSize(tileExtent.depth);

			const size_t batchSize = 16;
			std::vector<double> bindTimes;
			std::vector<VkSparseImageMemoryBind> sparseImageMemoryBinds;
			VkSparseImageMemoryBindInfo sparseImageMemoryBindInfo;

			const uint32_t num_tiles_i = imageExtent.width / tileExtent.width;
			const uint32_t num_tiles_j = imageExtent.height / tileExtent.height;
			const uint32_t num_tiles_k = imageExtent.depth / tileExtent.depth;
			const uint32_t numBinds = num_tiles_i * num_tiles_j * num_tiles_k;

			size_t bind = 0;
			std::cout << "Timing binds";
			for (uint32_t i = 0; i < num_tiles_i; i++) {
				for (uint32_t j = 0; j < num_tiles_j; j++) {
					for (uint32_t k = 0; k < num_tiles_k; k++) {

						sparseImageMemoryBinds.emplace_back(
							VkSparseImageMemoryBind{
								.subresource = VkImageSubresource{
									.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
									.mipLevel = 0,
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
								bindTimes.push_back(timer.getElapsedTimeMilliseconds());
							}

							sparseImageMemoryBinds.clear();
						}

						if (bind % (numBinds / 10) == 0) {
							std::cout << ".";
						}
						bind++;
					}
				}
			}

			std::filesystem::path filename = std::format("{} {}.txt",
				physicalDevice->deviceName(), physicalDevice->driverVersion());

			std::ofstream outFile(filename);
			if (outFile.is_open()) {
				outFile << device_info << std::endl;
				for (auto& bindTime : bindTimes) {
					outFile << bindTime << std::endl;
				}
			}
			std::cout << " Wrote results to: " << filename << std::endl << std::endl;
		}
		return EXIT_SUCCESS;
	}
	catch (const std::exception& e) {
		std::cerr << e.what();
		return EXIT_FAILURE;
	}
}
