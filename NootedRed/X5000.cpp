//! Copyright © 2022-2023 ChefKiss Inc. Licensed under the Thou Shalt Not Profit License version 1.5.
//! See LICENSE for details.

#include "X5000.hpp"
#include "NRed.hpp"
#include "PatcherPlus.hpp"
#include "X6000.hpp"
#include <Headers/kern_api.hpp>

static const char *pathRadeonX5000 = "/System/Library/Extensions/AMDRadeonX5000.kext/Contents/MacOS/AMDRadeonX5000";

static KernelPatcher::KextInfo kextRadeonX5000 {"com.apple.kext.AMDRadeonX5000", &pathRadeonX5000, 1, {}, {},
    KernelPatcher::KextInfo::Unloaded};

X5000 *X5000::callback = nullptr;

void X5000::init() {
    callback = this;
    lilu.onKextLoadForce(&kextRadeonX5000);
}

bool X5000::processKext(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size) {
    if (kextRadeonX5000.loadIndex == id) {
        NRed::callback->setRMMIOIfNecessary();

        UInt32 *orgChannelTypes = nullptr;
        mach_vm_address_t startHWEngines = 0;

        auto catalina = getKernelVersion() == KernelVersion::Catalina;
        SolveRequestPlus solveRequests[] = {
            {catalina ? "__ZZN37AMDRadeonX5000_AMDGraphicsAccelerator22getAdditionalQueueListEPPK18_"
                        "AMDQueueSpecifierE27additionalQueueList_Default" :
                        "__ZZN37AMDRadeonX5000_AMDGraphicsAccelerator19createAccelChannelsEbE12channelTypes",
                orgChannelTypes, kChannelTypesPattern},
            {"__ZN31AMDRadeonX5000_AMDGFX9PM4EngineC1Ev", this->orgGFX9PM4EngineConstructor},
            {"__ZN32AMDRadeonX5000_AMDGFX9SDMAEngineC1Ev", this->orgGFX9SDMAEngineConstructor},
            {"__ZN39AMDRadeonX5000_AMDAccelSharedUserClient5startEP9IOService", this->orgAccelSharedUCStart},
            {"__ZN39AMDRadeonX5000_AMDAccelSharedUserClient4stopEP9IOService", this->orgAccelSharedUCStop},
            {"__ZN35AMDRadeonX5000_AMDAccelVideoContext10gMetaClassE", NRed::callback->metaClassMap[0][0]},
            {"__ZN37AMDRadeonX5000_AMDAccelDisplayMachine10gMetaClassE", NRed::callback->metaClassMap[1][0]},
            {"__ZN34AMDRadeonX5000_AMDAccelDisplayPipe10gMetaClassE", NRed::callback->metaClassMap[2][0]},
            {"__ZN30AMDRadeonX5000_AMDAccelChannel10gMetaClassE", NRed::callback->metaClassMap[3][1]},
            {"__ZN28AMDRadeonX5000_IAMDHWChannel10gMetaClassE", NRed::callback->metaClassMap[4][0]},
            {"__ZN30AMDRadeonX5000_AMDGFX9Hardware32setupAndInitializeHWCapabilitiesEv",
                this->orgSetupAndInitializeHWCapabilities},
            {"__ZN26AMDRadeonX5000_AMDHardware14startHWEnginesEv", startHWEngines},
        };
        PANIC_COND(!SolveRequestPlus::solveAll(patcher, id, solveRequests, slide, size), "X5000",
            "Failed to resolve symbols");

        RouteRequestPlus requests[] = {
            {"__ZN32AMDRadeonX5000_AMDVega10Hardware17allocateHWEnginesEv", wrapAllocateHWEngines},
            {"__ZN32AMDRadeonX5000_AMDVega10Hardware32setupAndInitializeHWCapabilitiesEv",
                wrapSetupAndInitializeHWCapabilities},
            {"__ZN26AMDRadeonX5000_AMDHardware12getHWChannelE20_eAMD_HW_ENGINE_TYPE18_eAMD_HW_RING_TYPE",
                wrapGetHWChannel, this->orgGetHWChannel},
            {"__ZN30AMDRadeonX5000_AMDGFX9Hardware20initializeFamilyTypeEv", wrapInitializeFamilyType},
            {"__ZN30AMDRadeonX5000_AMDGFX9Hardware20allocateAMDHWDisplayEv", wrapAllocateAMDHWDisplay},
            {"__ZN41AMDRadeonX5000_AMDGFX9GraphicsAccelerator15newVideoContextEv", wrapNewVideoContext},
            {"__ZN31AMDRadeonX5000_IAMDSMLInterface18createSMLInterfaceEj", wrapCreateSMLInterface},
            {"__ZN26AMDRadeonX5000_AMDHWMemory17adjustVRAMAddressEy", wrapAdjustVRAMAddress,
                this->orgAdjustVRAMAddress},
            {"__ZN30AMDRadeonX5000_AMDGFX9Hardware25allocateAMDHWAlignManagerEv", wrapAllocateAMDHWAlignManager,
                this->orgAllocateAMDHWAlignManager},
            {"__ZN43AMDRadeonX5000_AMDVega10GraphicsAccelerator13getDeviceTypeEP11IOPCIDevice", wrapGetDeviceType},
            {"__ZN30AMDRadeonX5000_AMDGFX9Hardware20writeASICHangLogInfoEPPv", wrapReturnZero},
            {"__ZN4Addr2V27Gfx9Lib20HwlConvertChipFamilyEjj", wrapHwlConvertChipFamily, kHwlConvertChipFamilyPattern},
        };
        PANIC_COND(!RouteRequestPlus::routeAll(patcher, id, requests, slide, size), "X5000", "Failed to route symbols");

        bool ventura = getKernelVersion() >= KernelVersion::Ventura;
        bool ventura1304 = (ventura && getKernelMinorVersion() >= 5) || getKernelVersion() > KernelVersion::Ventura;
        if (!catalina) {
            RouteRequestPlus requests[] = {
                {"__ZN37AMDRadeonX5000_AMDGraphicsAccelerator9newSharedEv", wrapNewShared},
                {"__ZN37AMDRadeonX5000_AMDGraphicsAccelerator19newSharedUserClientEv", wrapNewSharedUserClient},
            };
            PANIC_COND(!RouteRequestPlus::routeAll(patcher, id, requests, slide, size), "X5000",
                "Failed to route newShared routes");

            if (ventura1304) {
                RouteRequestPlus request {"__ZN37AMDRadeonX5000_AMDGraphicsAccelerator23obtainAccelChannelGroupE11SS_"
                                          "PRIORITYP27AMDRadeonX5000_AMDAccelTask",
                    wrapObtainAccelChannelGroup1304, this->orgObtainAccelChannelGroup};
                PANIC_COND(!request.route(patcher, id, slide, size), "X5000",
                    "Failed to route obtainAccelChannelGroup");
            } else {
                RouteRequestPlus request {
                    "__ZN37AMDRadeonX5000_AMDGraphicsAccelerator23obtainAccelChannelGroupE11SS_PRIORITY",
                    wrapObtainAccelChannelGroup, this->orgObtainAccelChannelGroup};
                PANIC_COND(!request.route(patcher, id, slide, size), "X5000",
                    "Failed to route obtainAccelChannelGroup");
            }
        }

        if (catalina || ventura1304) {
            const LookupPatchPlus patch {&kextRadeonX5000, kAddrLibCreateOriginal, kAddrLibCreatePatched, 1};
            PANIC_COND(!patch.apply(patcher, slide, size), "X5000",
                "Failed to apply Catalina & Ventura 13.4+ Addr::Lib::Create patch");
        }

        if (catalina) {
            const LookupPatchPlus patch {&kextRadeonX5000, kCreateAccelChannelsOriginal, kCreateAccelChannelsPatched,
                2};
            PANIC_COND(!patch.apply(patcher, slide, size), "X5000", "Failed to patch createAccelChannels");

            auto dcn2 = NRed::callback->chipType >= ChipType::Renoir;
            UInt32 findNonBpp64 = 0x22222221;
            UInt32 replNonBpp64 = dcn2 ? Dcn2NonBpp64SwModeMask : Dcn1NonBpp64SwModeMask;
            UInt32 findBpp64 = 0x44444440;
            UInt32 replBpp64Pt2 = dcn2 ? Dcn2Bpp64SwModeMask : Dcn1Bpp64SwModeMask;
            UInt32 replBpp64 = replNonBpp64 ^ replBpp64Pt2;
            UInt32 findBpp64Pt2 = 0x66666661;
            const LookupPatchPlus patches[] = {
                {&kextRadeonX5000, reinterpret_cast<const UInt8 *>(&findNonBpp64),
                    reinterpret_cast<const UInt8 *>(&replNonBpp64), sizeof(UInt32), 2},
                {&kextRadeonX5000, reinterpret_cast<const UInt8 *>(&findBpp64),
                    reinterpret_cast<const UInt8 *>(&replBpp64), sizeof(UInt32), 1},
                {&kextRadeonX5000, reinterpret_cast<const UInt8 *>(&findBpp64Pt2),
                    reinterpret_cast<const UInt8 *>(&replBpp64Pt2), sizeof(UInt32), 1},
            };
            PANIC_COND(!LookupPatchPlus::applyAll(patcher, patches, slide, size), "X5000",
                "Failed to patch swizzle mode");

            PANIC_COND(MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS, "X5000",
                "Failed to enable kernel writing");
            *orgChannelTypes = 1;    //! Make VMPT use SDMA0 instead of SDMA1
            MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
            DBGLOG("X5000", "Applied SDMA1 patches");
        } else {
            const LookupPatchPlus patch {&kextRadeonX5000, kStartHWEnginesOriginal, kStartHWEnginesMask,
                kStartHWEnginesPatched, kStartHWEnginesMask, ventura ? 2U : 1};
            PANIC_COND(!patch.apply(patcher, startHWEngines, PAGE_SIZE), "X5000", "Failed to patch startHWEngines");

            if (NRed::callback->chipType >= ChipType::Renoir) {
                UInt32 findBpp64 = Dcn1Bpp64SwModeMask, replBpp64 = Dcn2Bpp64SwModeMask;
                UInt32 findNonBpp64 = Dcn1NonBpp64SwModeMask, replNonBpp64 = Dcn2NonBpp64SwModeMask;
                const LookupPatchPlus patches[] = {
                    {&kextRadeonX5000, reinterpret_cast<const UInt8 *>(&findBpp64),
                        reinterpret_cast<const UInt8 *>(&replBpp64), sizeof(UInt32), ventura1304 ? 2U : 4},
                    {&kextRadeonX5000, reinterpret_cast<const UInt8 *>(&findNonBpp64),
                        reinterpret_cast<const UInt8 *>(&replNonBpp64), sizeof(UInt32), ventura1304 ? 2U : 4},
                };
                PANIC_COND(!LookupPatchPlus::applyAll(patcher, patches, slide, size), "X5000",
                    "Failed to patch swizzle mode");
            }

            PANIC_COND(MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS, "X5000",
                "Failed to enable kernel writing");
            orgChannelTypes[5] = 1;    //! Fix createAccelChannels so that it only starts SDMA0
            orgChannelTypes[(getKernelVersion() >= KernelVersion::Monterey) ? 12 : 11] =
                0;    //! Fix getPagingChannel so that it gets SDMA0
            MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
            DBGLOG("X5000", "Applied SDMA1 patches");
        }

        return true;
    }

    return false;
}

bool X5000::wrapAllocateHWEngines(void *that) {
    auto catalina = getKernelVersion() == KernelVersion::Catalina;
    auto fieldBase = catalina ? 0x348 : 0x3B8;

    auto *pm4 = OSObject::operator new(0x340);
    callback->orgGFX9PM4EngineConstructor(pm4);
    getMember<void *>(that, fieldBase) = pm4;

    auto *sdma0 = OSObject::operator new(0x250);
    callback->orgGFX9SDMAEngineConstructor(sdma0);
    getMember<void *>(that, fieldBase + 0x8) = sdma0;

    auto *vcn0 = OSObject::operator new(0x2D8);
    X6000::callback->orgVCN2EngineConstructor(vcn0);
    getMember<void *>(that, fieldBase + (catalina ? 0x30 : 0x40)) = vcn0;

    return true;
}

struct HWCapability {
    enum : UInt64 {
        DisplayPipeCount = 0x04,      //! UInt32
        SECount = 0x34,               //! UInt32
        SHPerSE = 0x3C,               //! UInt32
        CUPerSH = 0x70,               //! UInt32
        HasUVD0 = 0x84,               //! bool
        HasVCE = 0x86,                //! bool
        HasVCN0 = 0x87,               //! bool
        HasSDMAPagingQueue = 0x98,    //! bool
    };
};

struct HWCapabilityCatalina {
    enum : UInt64 {
        DisplayPipeCount = 0x04,      //! UInt32
        SECount = 0x30,               //! UInt32
        SHPerSE = 0x34,               //! UInt32
        CUPerSH = 0x58,               //! UInt32
        HasUVD0 = 0x68,               //! bool
        HasVCE = 0x6A,                //! bool
        HasVCN0 = 0x6B,               //! bool
        HasSDMAPagingQueue = 0x7C,    //! bool
    };
};

template<typename T>
static inline void setHWCapability(void *that, UInt64 capability, T value) {
    getMember<T>(that, (getKernelVersion() >= KernelVersion::Ventura ? 0x30 : 0x28) + capability) = value;
}

void X5000::wrapSetupAndInitializeHWCapabilities(void *that) {
    bool isRavenDerivative = NRed::callback->chipType < ChipType::Renoir;
    char filename[128] = {0};
    snprintf(filename, arrsize(filename), "%s_gpu_info.bin", isRavenDerivative ? NRed::getChipName() : "renoir");
    auto &fwDesc = getFWDescByName(filename);
    auto *header = reinterpret_cast<const CommonFirmwareHeader *>(fwDesc.data);
    auto *gpuInfo = reinterpret_cast<const GPUInfoFirmware *>(fwDesc.data + header->ucodeOff);

    auto catalina = getKernelVersion() == KernelVersion::Catalina;
    setHWCapability<UInt32>(that, catalina ? HWCapabilityCatalina::SECount : HWCapability::SECount, gpuInfo->gcNumSe);
    setHWCapability<UInt32>(that, catalina ? HWCapabilityCatalina::SHPerSE : HWCapability::SHPerSE,
        gpuInfo->gcNumShPerSe);
    setHWCapability<UInt32>(that, catalina ? HWCapabilityCatalina::CUPerSH : HWCapability::CUPerSH,
        gpuInfo->gcNumCuPerSh);

    FunctionCast(wrapSetupAndInitializeHWCapabilities, callback->orgSetupAndInitializeHWCapabilities)(that);

    setHWCapability<UInt32>(that, catalina ? HWCapabilityCatalina::DisplayPipeCount : HWCapability::DisplayPipeCount,
        isRavenDerivative ? 4 : 6);
    setHWCapability<bool>(that, catalina ? HWCapabilityCatalina::HasUVD0 : HWCapability::HasUVD0, false);
    setHWCapability<bool>(that, catalina ? HWCapabilityCatalina::HasVCE : HWCapability::HasVCE, false);
    setHWCapability<bool>(that, catalina ? HWCapabilityCatalina::HasVCN0 : HWCapability::HasVCN0, true);
    setHWCapability<bool>(that, catalina ? HWCapabilityCatalina::HasSDMAPagingQueue : HWCapability::HasSDMAPagingQueue,
        false);
}

void *X5000::wrapGetHWChannel(void *that, UInt32 engineType, UInt32 ringId) {
    //! Redirect SDMA1 to SDMA0
    return FunctionCast(wrapGetHWChannel, callback->orgGetHWChannel)(that, (engineType == 2) ? 1 : engineType, ringId);
}

void X5000::wrapInitializeFamilyType(void *that) {
    getMember<UInt32>(that, getKernelVersion() == KernelVersion::Catalina ? 0x2B4 : 0x308) = AMDGPU_FAMILY_RAVEN;
}

void *X5000::wrapAllocateAMDHWDisplay(void *that) {
    return FunctionCast(wrapAllocateAMDHWDisplay, X6000::callback->orgAllocateAMDHWDisplay)(that);
}

void *X5000::wrapNewVideoContext(void *that) {
    return FunctionCast(wrapNewVideoContext, X6000::callback->orgNewVideoContext)(that);
}

void *X5000::wrapCreateSMLInterface(UInt32 configBit) {
    return FunctionCast(wrapCreateSMLInterface, X6000::callback->orgCreateSMLInterface)(configBit);
}

UInt64 X5000::wrapAdjustVRAMAddress(void *that, UInt64 addr) {
    auto ret = FunctionCast(wrapAdjustVRAMAddress, callback->orgAdjustVRAMAddress)(that, addr);
    return ret != addr ? (ret + NRed::callback->fbOffset) : ret;
}

void *X5000::wrapNewShared() { return FunctionCast(wrapNewShared, X6000::callback->orgNewShared)(); }

void *X5000::wrapNewSharedUserClient() {
    return FunctionCast(wrapNewSharedUserClient, X6000::callback->orgNewSharedUserClient)();
}

void *X5000::wrapAllocateAMDHWAlignManager() {
    auto ret = FunctionCast(wrapAllocateAMDHWAlignManager, callback->orgAllocateAMDHWAlignManager)();
    callback->hwAlignMgr = ret;

    callback->hwAlignMgrVtX5000 = getMember<UInt8 *>(ret, 0);
    callback->hwAlignMgrVtX6000 = IONewZero(UInt8, 0x238);

    memcpy(callback->hwAlignMgrVtX6000, callback->hwAlignMgrVtX5000, 0x128);
    *reinterpret_cast<mach_vm_address_t *>(callback->hwAlignMgrVtX6000 + 0x128) =
        X6000::callback->orgGetPreferredSwizzleMode2;
    memcpy(callback->hwAlignMgrVtX6000 + 0x130, callback->hwAlignMgrVtX5000 + 0x128, 0x230 - 0x128);
    return ret;
}

UInt32 X5000::wrapGetDeviceType() { return NRed::callback->chipType < ChipType::Renoir ? 0 : 9; }

UInt32 X5000::wrapReturnZero() { return 0; }

static void fixAccelGroup(void *that) {
    auto *&sdma1 = getMember<void *>(that, 0x18);
    sdma1 = sdma1 ?: getMember<void *>(that, 0x10);    //! Replace field with SDMA0, as we have no SDMA1
}

void *X5000::wrapObtainAccelChannelGroup(void *that, UInt32 priority) {
    auto ret = FunctionCast(wrapObtainAccelChannelGroup, callback->orgObtainAccelChannelGroup)(that, priority);
    if (ret) { fixAccelGroup(ret); }
    return ret;
}

void *X5000::wrapObtainAccelChannelGroup1304(void *that, UInt32 priority, void *task) {
    auto ret =
        FunctionCast(wrapObtainAccelChannelGroup1304, callback->orgObtainAccelChannelGroup)(that, priority, task);
    if (ret) { fixAccelGroup(ret); }
    return ret;
}

UInt32 X5000::wrapHwlConvertChipFamily(void *that, UInt32, UInt32) {
    auto &settings = getMember<Gfx9ChipSettings>(that, getKernelVersion() == KernelVersion::Catalina ? 0x5B18 : 0x5B10);
    bool renoir = NRed::callback->chipType >= ChipType::Renoir;
    settings.isArcticIsland = 1;
    settings.isRaven = 1;
    settings.depthPipeXorDisable = NRed::callback->chipType < ChipType::Raven2;
    settings.htileAlignFix = renoir;
    settings.applyAliasFix = renoir;
    settings.isDcn1 = 1;
    settings.metaBaseAlignFix = 1;
    return ADDR_CHIP_FAMILY_AI;
}
