#include <pal.h>
#include <palLib.h>
#include <palPlatform.h>
#include <palQueue.h>
#include <palCmdAllocator.h>
#include <palCmdBuffer.h>

#include <fmt/format.h>

#include <utility>
#include <cstdlib>
#include <cstdint>

struct PalError {
    Util::Result result;
};

void checkResult(Util::Result result) {
    if (Util::IsErrorResult(result))
        throw PalError{result};
}

template <typename PalType>
struct Unique {
    PalType* ptr;

    Unique(const Unique&) = delete;
    Unique& operator=(const Unique&) = delete;

    template <typename SizeFn, typename CreateFn>
    Unique(SizeFn size_fn, CreateFn create_fn) {
        Util::Result result = Util::Result::Success;
        size_t size = size_fn(&result);
        checkResult(result);

        // Pal types seem to be explicitly aligned to 16 bytes, which malloc should also do.
        void* memory = malloc(size);
        PalType* result_ptr;
        result = create_fn(memory, &result_ptr);
        if (Util::IsErrorResult(result)) {
            free(memory);
            throw PalError{result};
        }

        // Note, xgl seems to free memory by the result pointer and not by the allocated memory as well,
        // so it seems that placementAddr is always the same as the result addr.
        this->ptr = result_ptr;
    }

    Unique(Unique&& other):
        ptr(std::exchange(other.ptr, nullptr)) {
    }

    Unique& operator=(Unique&& other) {
        std::swap(this->ptr, other.ptr);
        return *this;
    }

    ~Unique() {
        if (this->ptr) {
            this->ptr->Destroy();
            free(this->ptr);
        }
    }

    PalType* operator->() const {
        return this->ptr;
    }
};

int main() {
    auto platform_info = Pal::PlatformCreateInfo{
        .pSettingsPath = "/etc/amd"
    };

    auto platform = Unique<Pal::IPlatform>(
        [](Util::Result* result) { return Pal::GetPlatformSize(); },
        [&](void* mem, Pal::IPlatform** platform) { return Pal::CreatePlatform(platform_info, mem, platform); }
    );

    fmt::print("Platform initialized\n");

    Pal::IDevice* devices[Pal::MaxDevices];
    uint32_t device_count = 0;
    checkResult(platform->EnumerateDevices(&device_count, devices));

    if (device_count == 0) {
        fmt::print("Platform has no devices :(\n");
        return EXIT_FAILURE;
    }

    fmt::print("Platform has {} device(s):\n", device_count);
    for (uint32_t i = 0; i < device_count; ++i) {
        Pal::DeviceProperties props;
        checkResult(devices[i]->GetProperties(&props));

        fmt::print("{}\n", props.gpuName);
        fmt::print("  graphics engines: {}\n", props.engineProperties[Pal::EngineTypeUniversal].engineCount);
        fmt::print("  compute engines: {}\n", props.engineProperties[Pal::EngineTypeCompute].engineCount);
        fmt::print("  dma engines: {}\n", props.engineProperties[Pal::EngineTypeDma].engineCount);
        fmt::print("  timer engines: {}\n", props.engineProperties[Pal::EngineTypeTimer].engineCount);
    }

    auto* device = devices[0];
    Pal::DeviceProperties props;
    checkResult(device->GetProperties(&props));

    fmt::print("Selected device '{}'\n", props.gpuName);

    auto finalize_info = Pal::DeviceFinalizeInfo{};
    finalize_info.requestedEngineCounts[Pal::EngineTypeCompute].engines = 1;
    checkResult(device->Finalize(finalize_info));

    fmt::print("Device initialized\n");

    if (props.engineProperties[Pal::EngineTypeCompute].engineCount == 0) {
        fmt::print("No compute engines :(\n");
        return EXIT_FAILURE;
    }
    if ((props.engineProperties[Pal::EngineTypeCompute].queueSupport & Pal::SupportQueueTypeCompute) == 0) {
        fmt::print("Compute engine does not support compute queue :(\n");
        return EXIT_FAILURE;
    }

    auto queue_create_info = Pal::QueueCreateInfo{
        .queueType = Pal::QueueTypeCompute,
        .engineType = Pal::EngineTypeCompute,
        .engineIndex = 0
    };
    auto queue = Unique<Pal::IQueue>(
        [&](Util::Result* result) { return device->GetQueueSize(queue_create_info, result); },
        [&](void* mem, Pal::IQueue** queue) { return device->CreateQueue(queue_create_info, mem, queue); }
    );

    fmt::print("Compute queue initialized\n");

    auto cmda_create_info = Pal::CmdAllocatorCreateInfo{};
    // Values taken from xgl/icd/settings/settings_xgl.json
    cmda_create_info.allocInfo[Pal::CommandDataAlloc] = {
        .allocHeap = Pal::GpuHeapGartUswc,
        .allocSize = 2097152,
        .suballocSize = 65536,
    };
    cmda_create_info.allocInfo[Pal::EmbeddedDataAlloc] = {
        .allocHeap = Pal::GpuHeapGartUswc,
        .allocSize = 131072,
        .suballocSize = 16384,
    };
    cmda_create_info.allocInfo[Pal::GpuScratchMemAlloc] = {
        .allocHeap = Pal::GpuHeapInvisible,
        .allocSize = 131072,
        .suballocSize = 16384,
    };
    auto cmda = Unique<Pal::ICmdAllocator>(
        [&](Util::Result* result) { return device->GetCmdAllocatorSize(cmda_create_info, result); },
        [&](void* mem, Pal::ICmdAllocator** cmda) { return device->CreateCmdAllocator(cmda_create_info, mem, cmda); }
    );

    fmt::print("Command allocator initialized\n");

    auto cmdbuf_create_info = Pal::CmdBufferCreateInfo{
        .pCmdAllocator = cmda.ptr,
        .queueType = Pal::QueueTypeCompute,
        .engineType = Pal::EngineTypeCompute
    };
    auto cmdbuf = Unique<Pal::ICmdBuffer>(
        [&](Util::Result* result) { return device->GetCmdBufferSize(cmdbuf_create_info, result); },
        [&](void* mem, Pal::ICmdBuffer** cmdbuf) { return device->CreateCmdBuffer(cmdbuf_create_info, mem, cmdbuf); }
    );

    fmt::print("Command buffer initialized\n");

    return EXIT_SUCCESS;
}
