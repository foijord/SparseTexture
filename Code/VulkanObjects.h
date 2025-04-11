#pragma once

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <Volk/volk.h>
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
#include <volk.h>
#endif

#include <vulkan/vk_enum_string_helper.h>

#include <vector>
#include <format>
#include <chrono>
#include <memory>
#include <string>
#include <iostream>
#include <stdexcept>

class Exception : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
	~Exception() override = default;
};

class VkException : public Exception {
public:
	using Exception::Exception;
	explicit VkException(VkResult error) :
		Exception(string_VkResult(error)),
		error(error)
	{
	}

	VkResult error;
};

#define THROW_ON_VULKAN_ERROR(__function__) {																		\
	VkResult result = (__function__);																				\
	if (result != VK_SUCCESS) {																						\
		throw VkException(result);																					\
	}																												\
}																													\
static_assert(true, "")


class VulkanPhysicalDevice {
public:
	VulkanPhysicalDevice() = delete;
	explicit VulkanPhysicalDevice(VkPhysicalDevice physicalDevice) :
		physicalDevice(physicalDevice)
	{
		vkGetPhysicalDeviceFeatures(this->physicalDevice, &this->physicalDeviceFeatures);
		vkGetPhysicalDeviceProperties(this->physicalDevice, &this->physicalDeviceProperties);

		uint32_t count;
		vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDevice, &count, nullptr);
		this->physicalDeviceQueueFamilyProperties.resize(count);
		vkGetPhysicalDeviceQueueFamilyProperties(this->physicalDevice, &count, this->physicalDeviceQueueFamilyProperties.data());

		this->queuePriorities.resize(this->physicalDeviceQueueFamilyProperties.size());
	}

	std::string deviceName() const
	{
		return std::string(this->physicalDeviceProperties.deviceName);
	}

	uint32_t deviceID() const
	{
		return this->physicalDeviceProperties.deviceID;
	}

	VkDeviceSize getSparseAddressSpaceSize() const
	{
		return this->physicalDeviceProperties.limits.sparseAddressSpaceSize;
	}

	std::string driverVersion() const
	{
		auto code = this->physicalDeviceProperties.driverVersion;
		// NVIDIA version scheme
		if (this->physicalDeviceProperties.vendorID == 4318) {
			return std::format("{}.{}.{}.{}", ((code >> 22) & 0x3ff), ((code >> 14) & 0x0ff), ((code >> 6) & 0x0ff), (code & 0x003f));
		}
#ifdef VK_USE_PLATFORM_WIN32_KHR
		// INTEL version scheme (only on Windows)
		if (this->physicalDeviceProperties.vendorID == 0x8086) {
			return std::format("{}.{}", (code >> 14), (code & 0x3fff));
		}
#endif
		// standard Vulkan versioning
		return std::format("{}.{}.{}", (code >> 22), ((code >> 12) & 0x3ff), (code & 0xfff));
	}

	VkImageFormatProperties getPhysicalDeviceImageFormatProperties(
		VkFormat format,
		VkImageType type,
		VkImageTiling tiling,
		VkImageUsageFlags usage,
		VkImageCreateFlags flags) const
	{
		VkImageFormatProperties imageformatproperties;
		THROW_ON_VULKAN_ERROR(vkGetPhysicalDeviceImageFormatProperties(this->physicalDevice, format, type, tiling, usage, flags, &imageformatproperties));
		return imageformatproperties;
	}

	uint32_t getMemoryTypeIndex(uint32_t memoryTypeBits, VkMemoryPropertyFlags required_flags) const
	{
		// memoryTypeBits is a bitmaskand contains one bit set for every supported memory type for the resource. Bit i is set if and only if
		// the memory type i in the VkPhysicalDeviceMemoryProperties structure for the physical device is supported for the resource.
		VkPhysicalDeviceMemoryProperties memory_properties;
		vkGetPhysicalDeviceMemoryProperties(this->physicalDevice, &memory_properties);

		for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
			if (((memoryTypeBits >> i) & 1) == 1 && (memory_properties.memoryTypes[i].propertyFlags & required_flags) == required_flags) {
				return i;
			}
		}
		throw Exception("VulkanDevice::getMemoryTypeIndex: could not find suitable memory type");
	}

	uint32_t getQueueFamilyIndex(VkQueueFlags required_flags, const std::vector<VkBool32>& filter) const
	{
		// check for exact match of required flags
		for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < this->physicalDeviceQueueFamilyProperties.size(); queueFamilyIndex++) {
			if (filter[queueFamilyIndex] && this->physicalDeviceQueueFamilyProperties[queueFamilyIndex].queueFlags == required_flags) {
				return queueFamilyIndex;
			}
		}
		// check for queue with all required flags set
		for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < this->physicalDeviceQueueFamilyProperties.size(); queueFamilyIndex++) {
			if (filter[queueFamilyIndex] && (this->physicalDeviceQueueFamilyProperties[queueFamilyIndex].queueFlags & required_flags) == required_flags) {
				return queueFamilyIndex;
			}
		}
		throw Exception("VulkanPhysicalDevice::getQueueFamilyIndex: could not find queue with required properties");
	}

	uint32_t getQueueFamilyIndex(VkQueueFlags required_flags, VkSurfaceKHR surface = VK_NULL_HANDLE) const
	{
		std::vector<VkBool32> filter(this->physicalDeviceQueueFamilyProperties.size(), VK_TRUE);
		if (surface != VK_NULL_HANDLE) {
			for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < this->physicalDeviceQueueFamilyProperties.size(); queueFamilyIndex++) {
				THROW_ON_VULKAN_ERROR(vkGetPhysicalDeviceSurfaceSupportKHR(this->physicalDevice, queueFamilyIndex, surface, &filter[queueFamilyIndex]));
			}
		}
		return this->getQueueFamilyIndex(required_flags, filter);
	}

	uint32_t addQueue(uint32_t queueFamilyIndex, float priority = 1.0f, VkDeviceQueueCreateFlags flags = 0)
	{
		auto max_queue_count = this->physicalDeviceQueueFamilyProperties[queueFamilyIndex].queueCount;
		auto current_queue_count = this->queuePriorities[queueFamilyIndex].size();

		if (current_queue_count < max_queue_count) {

			if (this->queuePriorities[queueFamilyIndex].empty()) {
				this->deviceQueueCreateInfos.emplace_back(
					VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, // sType
					nullptr,									// pNext
					flags,										// flags
					queueFamilyIndex);							// queueFamilyIndex
			}

			this->queuePriorities[queueFamilyIndex].push_back(priority);
			for (auto& queueCreateInfo : this->deviceQueueCreateInfos) {
				if (queueCreateInfo.queueFamilyIndex == queueFamilyIndex) {
					queueCreateInfo.queueCount = static_cast<uint32_t>(this->queuePriorities[queueCreateInfo.queueFamilyIndex].size());
					queueCreateInfo.pQueuePriorities = this->queuePriorities[queueCreateInfo.queueFamilyIndex].data();
				}
			}
		}
		else {
			std::cout << "Cannot create more queues of queueFamilyIndex " << queueFamilyIndex;
		}
		return static_cast<uint32_t>(this->queuePriorities[queueFamilyIndex].size() - 1);
	}


	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceFeatures physicalDeviceFeatures;
	VkPhysicalDeviceProperties physicalDeviceProperties;
	std::vector<VkQueueFamilyProperties> physicalDeviceQueueFamilyProperties;
	std::vector<VkDeviceQueueCreateInfo> deviceQueueCreateInfos;
	std::vector<std::vector<float>> queuePriorities;
};


class VulkanInstance {
public:
	VulkanInstance(
		const std::vector<const char*>& enabledLayers = {},
		const std::vector<const char*>& enabledExtensions = {})
	{
		VkApplicationInfo applicationInfo{
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pNext = nullptr,
			.pApplicationName = "SparseTexture",
			.applicationVersion = 1,
			.pEngineName = "TestEngine",
			.engineVersion = 1,
			.apiVersion = VK_API_VERSION_1_3,
		};

		VkInstanceCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.pApplicationInfo = &applicationInfo,
			.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size()),
			.ppEnabledLayerNames = enabledLayers.data(),
			.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
			.ppEnabledExtensionNames = enabledExtensions.data(),
		};

		THROW_ON_VULKAN_ERROR(vkCreateInstance(&createInfo, nullptr, &this->instance));
		volkLoadInstance(this->instance);
	}

	~VulkanInstance()
	{
		vkDestroyInstance(this->instance, nullptr);
	}

	std::vector<VkPhysicalDevice> getPhysicalDevices() const
	{
		uint32_t device_count;
		THROW_ON_VULKAN_ERROR(vkEnumeratePhysicalDevices(this->instance, &device_count, nullptr));

		std::vector<VkPhysicalDevice> physicalDevices(device_count);
		THROW_ON_VULKAN_ERROR(vkEnumeratePhysicalDevices(this->instance, &device_count, physicalDevices.data()));

		return physicalDevices;
	}

	std::vector<std::shared_ptr<VulkanPhysicalDevice>> getVulkanPhysicalDevices() const
	{
		std::vector<std::shared_ptr<VulkanPhysicalDevice>> devices;
		for (auto& physicalDevice : this->getPhysicalDevices()) {
			devices.emplace_back(std::make_shared<VulkanPhysicalDevice>(physicalDevice));
		}
		return devices;
	}

	VkInstance instance{ nullptr };
};


class VulkanDevice {
public:
	VulkanDevice(
		std::shared_ptr<VulkanInstance> instance,
		std::shared_ptr<VulkanPhysicalDevice> physicalDevice,
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos) :
		instance(std::move(instance)),
		physicalDevice(std::move(physicalDevice))
	{
		VkPhysicalDeviceFeatures physicalDeviceFeatures{
			.sparseBinding = VK_TRUE,
			.sparseResidencyImage2D = VK_TRUE,
			.sparseResidencyImage3D = VK_TRUE,
		};

		std::vector<const char*> enabledLayerNames{};
		std::vector<const char*> enabledExtensionNames{ VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME };

		VkDeviceCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
			.pQueueCreateInfos = queueCreateInfos.data(),
			.enabledLayerCount = static_cast<uint32_t>(enabledLayerNames.size()),
			.ppEnabledLayerNames = enabledLayerNames.data(),
			.enabledExtensionCount = static_cast<uint32_t>(enabledExtensionNames.size()),
			.ppEnabledExtensionNames = enabledExtensionNames.data(),
			.pEnabledFeatures = &physicalDeviceFeatures,
		};

		THROW_ON_VULKAN_ERROR(vkCreateDevice(this->physicalDevice->physicalDevice, &createInfo, nullptr, &this->device));
	}

	VkMemoryRequirements getMemoryRequirements(VkImage image)
	{
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(this->device, image, &memory_requirements);
		return memory_requirements;
	}

	~VulkanDevice()
	{
		vkDestroyDevice(this->device, nullptr);
	}

	std::shared_ptr<VulkanInstance> instance{ nullptr };
	std::shared_ptr<VulkanPhysicalDevice> physicalDevice{ nullptr };
	VkDevice device{ nullptr };
};


class VulkanFence {
public:
	VulkanFence(std::shared_ptr<VulkanDevice> device, VkFenceCreateFlags flags = 0) :
		device(std::move(device))
	{
		VkFenceCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.pNext = nullptr,
			.flags = flags,
		};

		THROW_ON_VULKAN_ERROR(vkCreateFence(this->device->device, &createInfo, nullptr, &this->fence));
	}

	~VulkanFence()
	{
		vkDestroyFence(this->device->device, this->fence, nullptr);
	}

	void waitAndReset()
	{
		THROW_ON_VULKAN_ERROR(vkWaitForFences(this->device->device, 1, &this->fence, VK_TRUE, UINT64_MAX));
		THROW_ON_VULKAN_ERROR(vkResetFences(this->device->device, 1, &this->fence));
	}

	std::shared_ptr<VulkanDevice> device{ nullptr };
	VkFence fence{ nullptr };
};


class VulkanQueue {
public:
	VulkanQueue(
		std::shared_ptr<VulkanDevice> device,
		uint32_t queueFamilyIndex,
		uint32_t queueIndex) :
		device(std::move(device))
	{
		vkGetDeviceQueue(this->device->device, queueFamilyIndex, queueIndex, &this->queue);
	}

	void bindSparse(
		VkSparseImageMemoryBindInfo sparseImageMemoryBindInfo,
		VkFence fence = VK_NULL_HANDLE)
	{
		VkBindSparseInfo bindSparseInfo{
			.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO,
			.pNext = nullptr,
			.waitSemaphoreCount = 0,
			.pWaitSemaphores = nullptr,
			.bufferBindCount = 0,
			.pBufferBinds = nullptr,
			.imageOpaqueBindCount = 0,
			.pImageOpaqueBinds = nullptr,
			.imageBindCount = 1,
			.pImageBinds = &sparseImageMemoryBindInfo,
			.signalSemaphoreCount = 0,
			.pSignalSemaphores = nullptr,
		};
		THROW_ON_VULKAN_ERROR(vkQueueBindSparse(this->queue, 1, &bindSparseInfo, fence));
	}

	std::shared_ptr<VulkanDevice> device{ nullptr };
	VkQueue queue;
};


class VulkanImage {
public:
	struct Config {
		VkImageCreateFlags flags{ 0 };
		VkImageType imageType{ VK_IMAGE_TYPE_MAX_ENUM };
		VkFormat format{ VK_FORMAT_UNDEFINED };
		VkExtent3D extent{ .width = 0, .height = 0, .depth = 0 };
		uint32_t mipLevels{ 1 };
		uint32_t arrayLayers{ 1 };
		VkSampleCountFlagBits samples{ VK_SAMPLE_COUNT_1_BIT };
		VkImageTiling tiling{ VK_IMAGE_TILING_OPTIMAL };
		VkImageUsageFlags usage{ 0 };
		VkSharingMode sharingMode{ VK_SHARING_MODE_EXCLUSIVE };
		std::vector<uint32_t> queueFamilyIndices;
		VkImageLayout initialLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
	};

	VulkanImage(std::shared_ptr<VulkanDevice> device, const Config& config) :
		device(std::move(device))
	{
		VkImageCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.pNext = nullptr,
			.flags = config.flags,
			.imageType = config.imageType,
			.format = config.format,
			.extent = config.extent,
			.mipLevels = config.mipLevels,
			.arrayLayers = config.arrayLayers,
			.samples = config.samples,
			.tiling = config.tiling,
			.usage = config.usage,
			.sharingMode = config.sharingMode,
			.queueFamilyIndexCount = static_cast<uint32_t>(config.queueFamilyIndices.size()),
			.pQueueFamilyIndices = config.queueFamilyIndices.data(),
			.initialLayout = config.initialLayout,
		};

		THROW_ON_VULKAN_ERROR(vkCreateImage(this->device->device, &createInfo, nullptr, &this->image));
	}

	~VulkanImage()
	{
		vkDestroyImage(this->device->device, this->image, nullptr);
	}

	std::shared_ptr<VulkanDevice> device{ nullptr };
	VkImage image{ nullptr };
};


class VulkanMemory {
public:
	VulkanMemory(
		std::shared_ptr<VulkanDevice> device,
		const VkMemoryRequirements& memoryRequirements,
		VkMemoryPropertyFlags memoryFlags) :
		device(std::move(device))
	{
		uint32_t memoryTypeIndex = this->device->physicalDevice->getMemoryTypeIndex(
			memoryRequirements.memoryTypeBits,
			memoryFlags);

		VkMemoryAllocateInfo allocate_info{
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.pNext = nullptr,
			.allocationSize = memoryRequirements.size,
			.memoryTypeIndex = memoryTypeIndex,
		};

		THROW_ON_VULKAN_ERROR(vkAllocateMemory(this->device->device, &allocate_info, nullptr, &this->memory));
	}

	~VulkanMemory()
	{
		vkFreeMemory(this->device->device, this->memory, nullptr);
	}

	std::shared_ptr<VulkanDevice> device{ nullptr };
	VkDeviceMemory memory{ nullptr };
};


class Timer {
public:
	~Timer() = default;

	Timer()
	{
		this->start_time = std::chrono::steady_clock::now();
	}

	double getElapsedTimeSeconds() const
	{
		return std::chrono::duration_cast<std::chrono::duration<double>>(
			std::chrono::steady_clock::now() - this->start_time).count();
	}

	std::chrono::time_point<std::chrono::steady_clock> start_time;
};
