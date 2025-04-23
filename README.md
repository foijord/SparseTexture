# Vulkan Sparse Texture Binding Performance
Sparse resources in Vulkan enable virtual texture systems, often called giga-textures, by allowing applications to bind memory pages to large images on demand. This approach is ideal for managing massive textures efficiently, but its effectiveness depends on the speed and concurrency of binding operations. If binding tiles to a sparse image via **vkQueueBindSparse** is slow and blocking, sparse resources become impractical for real-time applications like virtual texturing in games or rendering engines.

This program investigates the performance of vkQueueBindSparse by creating a large sparse 3D image and binding its memory pages in batches of 16, progressively covering the entire image extent. The execution time of each vkQueueBindSparse call is measured using a high-resolution timer and logged to a text file named after the device and driver version (e.g., NVIDIA RTX A6000 551.86.0.0.txt). The test uses a 4096×4096×1024 image with 64×64×64 tiles, constrained by the device’s sparse address space and maximum image extent, both queried via Vulkan APIs. The source code is designed to be self-explanatory, offering detailed insight into the binding process—refer to it for implementation specifics.

## The results reveal significant performance differences across GPU vendors:
- **NVIDIA (e.g., RTX A6000):** Binding times increase dramatically as more tiles are bound, with a sparse address space of just 1 TiB—insufficient for a single maximum-extent image (16384×16384×16384). The operation blocks not only the calling thread but also other threads and processes. For instance, running multiple instances of this program slows all sparse binding operations system-wide, suggesting a global driver lock.

- **Intel (e.g., Arc A770):** Exhibits low, consistent bind times (e.g., ~steady performance), with a more reasonable 16 TiB sparse address space, making it more viable for sparse textures.

- **AMD (e.g., RX 7900 XT):** Shows progressively worsening bind times, similar to NVIDIA, despite a massive 256 TiB sparse address space.

The test performs each bind and waits for a fence in the main thread. One might expect that moving the fence wait to a separate thread would isolate the binding latency, preserving application framerate. However, on NVIDIA hardware, this is ineffective: vkQueueBindSparse blocks globally, affecting all threads and even separate processes. This behavior contradicts the intended concurrency of Vulkan’s queue system, where queue submissions should not stall unrelated operations.
vkQueueBindSparse is a queue operation with an optional fence to track completion. This implies an asynchronous design, where control should be returned immediately. NVIDIA’s implementation, by contrast, introduces significant delays that scale with image coverage and impact system-wide performance, undermining Vulkan’s performance model.

## Program Details
- **Setup:** Creates a Vulkan instance, selects a physical device supporting sparse binding, and configures a 3D sparse image (VK_IMAGE_TYPE_3D, VK_FORMAT_R8_SNORM, 4096×4096×1024, sparse residency enabled). Allocates 1 GiB of device-local memory for tiles.

- **Binding:** Divides the image into 64×64×64 tiles, binding 16 at a time using vkQueueBindSparse. Measures time from submission to fence signal.

- **Output:** Logs bind times to a device-specific .txt file and prints sparse address space (in TiB) and max image extent.

## Key Findings
- **Performance Scaling:** Larger image extents exacerbate binding times, especially on NVIDIA and AMD, where times grow non-linearly (see plotted results: nvidia.png, amd.png). Intel maintains stable performance (intel.png).

- **Sparse Address Space:** NVIDIA’s 1 TiB limit is restrictive compared to Intel’s 16 TiB and AMD’s 256 TiB, limiting maximum texture sizes.

- **Blocking Behavior:** NVIDIA’s global blocking across threads and processes renders threaded waits ineffective, a critical flaw for real-time sparse texture updates.

## Implications
For virtual texture systems, NVIDIA’s slow, blocking vkQueueBindSparse renders sparse resources unusable in performance-sensitive scenarios. Intel offers a promising alternative with fast, predictable binds, while AMD’s large address space is offset by degrading performance. Developers should analyze the generated bind time logs to assess sparse viability on their hardware, particularly as texture sizes increase.

## Build and run on Linux
```
SparseTexture$ docker build -f Scripts/Dockerfile -t sparsetexture-build Scripts
SparseTexture$ ./Scripts/run.sh
SparseTexture$ Build/SparseTexture
```

## Build and run on Windows
Open a developer powershell for Visual Studio 2022
```
SparseTexture> mkdir Build
SparseTexture> cd Build
SparseTexture\Build> cmake.exe ..\Code\
```
This generates the VS solution in the Build folder. Open, build and run.

## Example Output on my test system
```
Intel(R) Arc(TM) A770 Graphics, Driver version: 101.6734
Sparse address space: 16 TiB
Image max extent: (16384, 16384, 2048)
Timing binds........... Wrote results to: "Intel(R) Arc(TM) A770 Graphics 101.6734.txt"

AMD Radeon RX 7900 XT, Driver version: 2.0.302
Sparse address space: 255.9765625 TiB
Image max extent: (16384, 16384, 8192)
Timing binds........... Wrote results to: "AMD Radeon RX 7900 XT 2.0.302.txt"

NVIDIA RTX A6000, Driver version: 572.83.0.0
Sparse address space: 1 TiB
Image max extent: (16384, 16384, 16384)
Timing binds........... Wrote results to: "NVIDIA RTX A6000 572.83.0.0.txt"
```

## Bind times plotted:

![AMD Radeon RX 7900 XT 2.0.331.png](Runs//AMD%20Radeon%20RX%207900%20XT%202.0.331.png)
![Intel(R) Arc(TM) A770 Graphics 101.6734.png](Runs//Intel(R)%20Arc(TM)%20A770%20Graphics%20101.6734.png)
![NVIDIA RTX A6000 572.83.0.0.png](Runs//NVIDIA%20RTX%20A6000%20572.83.0.0.png)
![NVIDIA RTX A6000 576.2.0.0.png](Runs//NVIDIA%20RTX%20A6000%20576.2.0.0.png)
