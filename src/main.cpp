#include <pal.h>
#include <palLib.h>
#include <palPlatform.h>
#include <palQueue.h>
#include <palCmdAllocator.h>
#include <palCmdBuffer.h>

#include <fmt/format.h>

#include <stdexcept>
#include <utility>
#include <cstdlib>
#include <cstdint>

extern const char test_elf_start[] asm("_binary_test_elf_start");
extern const char test_elf_end[] asm("_binary_test_elf_end");

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

Unique<Pal::IPlatform> create_platform() {
    auto platform_info = Pal::PlatformCreateInfo{
        .pSettingsPath = "/etc/amd"
    };

    return Unique<Pal::IPlatform>(
        [](Util::Result* result) { return Pal::GetPlatformSize(); },
        [&](void* mem, Pal::IPlatform** platform) { return Pal::CreatePlatform(platform_info, mem, platform); }
    );
}

Pal::IDevice* select_device(Pal::IPlatform* platform) {
    Pal::IDevice* devices[Pal::MaxDevices];
    uint32_t device_count = 0;
    checkResult(platform->EnumerateDevices(&device_count, devices));

    if (device_count == 0) {
        throw std::runtime_error("Platform has no devices");
    }

    fmt::print("Platform has {} device(s):\n", device_count);
    for (uint32_t i = 0; i < device_count; ++i) {
        Pal::DeviceProperties props;
        checkResult(devices[i]->GetProperties(&props));

        fmt::print("{}\n", props.gpuName);
        fmt::print("  graphics engines: {}\n", props.engineProperties[Pal::EngineTypeUniversal].engineCount);
        fmt::print("  compute engines: {}\n", props.engineProperties[Pal::EngineTypeCompute].engineCount);
        fmt::print("  dma engines: {}\n", props.engineProperties[Pal::EngineTypeDma].engineCount);
        fmt::print("  max user data entries: {}\n", props.gfxipProperties.maxUserDataEntries);
        fmt::print("  supports HSA abi: {}\n", props.gfxipProperties.flags.supportHsaAbi ? "true" : "false");
        fmt::print("  buffer view descriptor size: {}\n", props.gfxipProperties.srdSizes.bufferView);
    }

    return devices[0];
}

Unique<Pal::IQueue> create_queue(Pal::IDevice* device, const Pal::DeviceProperties& props) {
    if (props.engineProperties[Pal::EngineTypeCompute].engineCount == 0) {
        throw std::runtime_error("Device has no compute engines");
    }
    if ((props.engineProperties[Pal::EngineTypeCompute].queueSupport & Pal::SupportQueueTypeCompute) == 0) {
        throw std::runtime_error("Compute engine does not support compute queue ???");
    }

    auto queue_create_info = Pal::QueueCreateInfo{
        .queueType = Pal::QueueTypeCompute,
        .engineType = Pal::EngineTypeCompute,
        .engineIndex = 0
    };
    return Unique<Pal::IQueue>(
        [&](Util::Result* result) { return device->GetQueueSize(queue_create_info, result); },
        [&](void* mem, Pal::IQueue** queue) { return device->CreateQueue(queue_create_info, mem, queue); }
    );
}

Unique<Pal::ICmdAllocator> create_cmd_allocator(Pal::IDevice* device) {
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

    return Unique<Pal::ICmdAllocator>(
        [&](Util::Result* result) { return device->GetCmdAllocatorSize(cmda_create_info, result); },
        [&](void* mem, Pal::ICmdAllocator** cmda) { return device->CreateCmdAllocator(cmda_create_info, mem, cmda); }
    );
}

Unique<Pal::ICmdBuffer> create_cmd_buffer(Pal::IDevice* device, Pal::ICmdAllocator* cmda) {
    auto cmdbuf_create_info = Pal::CmdBufferCreateInfo{
        .pCmdAllocator = cmda,
        .queueType = Pal::QueueTypeCompute,
        .engineType = Pal::EngineTypeCompute
    };
    return Unique<Pal::ICmdBuffer>(
        [&](Util::Result* result) { return device->GetCmdBufferSize(cmdbuf_create_info, result); },
        [&](void* mem, Pal::ICmdBuffer** cmdbuf) { return device->CreateCmdBuffer(cmdbuf_create_info, mem, cmdbuf); }
    );
}

Unique<Pal::IPipeline> create_pipeline(Pal::IDevice* device) {
    auto pipeline_create_info = Pal::ComputePipelineCreateInfo{
        .pPipelineBinary = test_elf_start,
        .pipelineBinarySize = static_cast<size_t>(test_elf_end - test_elf_start)
    };

    return Unique<Pal::IPipeline>(
        [&](Util::Result* result) { return device->GetComputePipelineSize(pipeline_create_info, result); },
        [&](void* mem, Pal::IPipeline** pipeline) { return device->CreateComputePipeline(pipeline_create_info, mem, pipeline); }
    );
}

int main() {
    auto platform = create_platform();
    fmt::print("Platform initialized\n");

    auto* device = select_device(platform.ptr);
    Pal::DeviceProperties props;
    checkResult(device->GetProperties(&props));
    fmt::print("Selected device '{}'\n", props.gpuName);

    auto finalize_info = Pal::DeviceFinalizeInfo{};
    finalize_info.requestedEngineCounts[Pal::EngineTypeCompute].engines = 1;
    checkResult(device->CommitSettingsAndInit());
    checkResult(device->Finalize(finalize_info));
    fmt::print("Device initialized\n");

    auto queue = create_queue(device, props);
    fmt::print("Compute queue initialized\n");

    auto cmda = create_cmd_allocator(device);
    fmt::print("Command allocator initialized\n");

    auto cmd_buf = create_cmd_buffer(device, cmda.ptr);
    fmt::print("Command buffer initialized\n");

    auto pipeline = create_pipeline(device);
    fmt::print("Pipeline initialized\n");

    return EXIT_SUCCESS;
}
