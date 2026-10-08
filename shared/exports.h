/* d3d11 export surface, forwarded lazily to System32\d3d11.dll.
 *
 * Order is a hard contract with proxy.def: entry N here is proxy_N there and
 * is resolved by the thunk that passes N to proxy_resolve(). The list must
 * stay byte-identical to the real d3d11.dll export table (name and ordinal),
 * or the game fails to load the graphics device. tools/pe_exports.py checks it.
 */
#ifndef HSLR_WAITKEY_D3D11_EXPORTS_H
#define HSLR_WAITKEY_D3D11_EXPORTS_H
#include <windows.h>
FARPROC proxy_slots[51];
static const char* const export_names[51]={
    "D3D11CreateDeviceForD3D12",
    "D3DKMTCloseAdapter",
    "D3DKMTDestroyAllocation",
    "D3DKMTDestroyContext",
    "D3DKMTDestroyDevice",
    "D3DKMTDestroySynchronizationObject",
    "D3DKMTPresent",
    "D3DKMTQueryAdapterInfo",
    "D3DKMTSetDisplayPrivateDriverFormat",
    "D3DKMTSignalSynchronizationObject",
    "D3DKMTUnlock",
    "D3DKMTWaitForSynchronizationObject",
    "EnableFeatureLevelUpgrade",
    "OpenAdapter10",
    "OpenAdapter10_2",
    "CreateDirect3D11DeviceFromDXGIDevice",
    "CreateDirect3D11SurfaceFromDXGISurface",
    "D3D11CoreCreateDevice",
    "D3D11CoreCreateLayeredDevice",
    "D3D11CoreGetLayeredDeviceSize",
    "D3D11CoreRegisterLayers",
    "D3D11CreateDevice",
    "D3D11CreateDeviceAndSwapChain",
    "D3D11On12CreateDevice",
    "D3DKMTCreateAllocation",
    "D3DKMTCreateContext",
    "D3DKMTCreateDevice",
    "D3DKMTCreateSynchronizationObject",
    "D3DKMTEscape",
    "D3DKMTGetContextSchedulingPriority",
    "D3DKMTGetDeviceState",
    "D3DKMTGetDisplayModeList",
    "D3DKMTGetMultisampleMethodList",
    "D3DKMTGetRuntimeData",
    "D3DKMTGetSharedPrimaryHandle",
    "D3DKMTLock",
    "D3DKMTOpenAdapterFromHdc",
    "D3DKMTOpenResource",
    "D3DKMTQueryAllocationResidency",
    "D3DKMTQueryResourceInfo",
    "D3DKMTRender",
    "D3DKMTSetAllocationPriority",
    "D3DKMTSetContextSchedulingPriority",
    "D3DKMTSetDisplayMode",
    "D3DKMTSetGammaRamp",
    "D3DKMTSetVidPnSourceOwner",
    "D3DKMTWaitForVerticalBlankEvent",
    "D3DPerformance_BeginEvent",
    "D3DPerformance_EndEvent",
    "D3DPerformance_GetStatus",
    "D3DPerformance_SetMarker",
};
#endif
