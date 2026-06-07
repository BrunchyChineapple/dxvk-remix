/*
 * Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "log/log.h"
#include "util_bridgecommand.h"
#include "util_devicecommand.h"
#include "util_remixapi.h"
#include "d3d9_texture.h"   // Direct3DTexture9_LSS (client texture proxy -> getId) for dxvk_GetTextureHash

using namespace remixapi::util;

namespace remixapi {

bool g_bInterfaceInitialized = false;
PFN_remixapi_BridgeCallback g_beginSceneCallback = nullptr;
PFN_remixapi_BridgeCallback g_endSceneCallback = nullptr;
PFN_remixapi_BridgeCallback g_presentCallback = nullptr;

template<typename T>
inline void send(ClientMessage& msg, const T& val) {
  msg.send_data(sizeof(T), &val);
}

template<>
inline void send(ClientMessage& msg, const remixapi_Float3D& vec3) {
  send(msg, vec3.x);
  send(msg, vec3.y);
  send(msg, vec3.z);
}

template<>
inline void send(ClientMessage& msg, const remixapi_Path& path) {
  const auto nonNullPath = (path) ? (path) : L"";
  msg.send_data(wcslen(nonNullPath) * sizeof(wchar_t), nonNullPath);
}

template<>
inline void send(ClientMessage& msg, const char* const& c_str) {
  msg.send_data((uint32_t) strlen(c_str) + 1, c_str);
}

template<typename RemixApiHandleT>
inline void sendHandle(ClientMessage& msg, const Handle<RemixApiHandleT>& handle) {
  msg.send_data(handle.uid);
}

template<>
inline void send(ClientMessage& msg, const Bool& b) {
  uint32_t boolVal = 0x0;
  boolVal |= (uint8_t)b;
  msg.send_data(boolVal);
}

template<typename SerializableT>
auto serializeAndSend(ClientMessage& msg, const SerializableT& serializable) {
  static_assert(is_serializable_v<SerializableT>, "serializeAndSend(...)  may only be called with defined Serializable<T> types");
  msg.send_data(ToRemixApiStructEnum<SerializableT::BaseT>);
  const auto serializableSize = serializable.size();
  auto pSlzd = new uint8_t[serializableSize];
  serializable.serialize(pSlzd);
  msg.send_data(serializableSize, pSlzd);
  delete pSlzd;
}


remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateMaterial(
  const remixapi_MaterialInfo* info,
  remixapi_MaterialHandle*     out_handle) {

  ASSERT_REMIXAPI_PFN_TYPE(remixapi_CreateMaterial);
  assert(info->sType == REMIXAPI_STRUCT_TYPE_MATERIAL_INFO);

  MaterialHandle newHandle;
  {
    ClientMessage c(Commands::RemixApi_CreateMaterial);

    serializeAndSend<serialize::MaterialInfo>(c, *info);

    // For each valid pNext, we will send a true-valued bool to indicate that
    // server must read another extension. If it reads false, it knows that it
    // is done reading.
    // send(c, Bool::True); -> CONTINUE
    // send(c, Bool::False); -> STOP
    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      infoItr = pNext;
      switch (getSType(pNext)) {
        case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT:
        {
          auto* pOpaqueMat = static_cast<const remixapi_MaterialInfoOpaqueEXT* const>(infoItr);
          send(c, Bool::True); 
          serializeAndSend<serialize::MaterialInfoOpaque>(c, *pOpaqueMat);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT:
        {
          auto* pOpaqueSubsurfaceMat = static_cast<const remixapi_MaterialInfoOpaqueSubsurfaceEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::MaterialInfoOpaqueSubsurface>(c, *pOpaqueSubsurfaceMat);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT:
        {
          auto* pTranslucentMat = static_cast<const remixapi_MaterialInfoTranslucentEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::MaterialInfoTranslucent>(c, *pTranslucentMat);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_PORTAL_EXT:
        {
          auto* pPortalMat = static_cast<const remixapi_MaterialInfoPortalEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::MaterialInfoPortal>(c, *pPortalMat);
          break;
        }
        default:
        {
          Logger::warn("[remixapi_CreateMaterial] Unknown sType. Skipping.");
          break;
        }
      }
      infoItr = pNext;
    }
    send(c, Bool::False);
    sendHandle(c, newHandle);
  }

  *out_handle = newHandle;

  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DestroyMaterial(remixapi_MaterialHandle handle) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DestroyMaterial);
  MaterialHandle materialHandle(handle);
  if(!materialHandle.isValid()) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_DestroyMaterial);
    sendHandle(c, materialHandle);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateMesh(
  const remixapi_MeshInfo* info,
  remixapi_MeshHandle*     out_handle) {

  ASSERT_REMIXAPI_PFN_TYPE(remixapi_CreateMesh);
  assert(info->sType == REMIXAPI_STRUCT_TYPE_MESH_INFO);

  MeshHandle newHandle;
  {
    ClientMessage c(Commands::RemixApi_CreateMesh);
    
    serializeAndSend<serialize::MeshInfo>(c, *info);

    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      switch (getSType(pNext)) {
        default:
        {
          Logger::warn("[remixapi_CreateMesh] Unknown sType. Skipping.");
          break;
        }
      }
    }
    sendHandle(c, newHandle);
  }
  
  *out_handle = newHandle;

  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateMeshBatched(
  const remixapi_MeshInfo* info,
  remixapi_MeshHandle*     out_handle) {

  ASSERT_REMIXAPI_PFN_TYPE(remixapi_CreateMeshBatched);
  assert(info->sType == REMIXAPI_STRUCT_TYPE_MESH_INFO);

  // Batched mesh creation marshals the identical remixapi_MeshInfo payload as
  // remixapi_CreateMesh; the only difference is the server-side verb it invokes
  // (the renderer defers DXVK buffer allocation / asset-replacer registration to
  // the next render-thread flush). Mirror CreateMesh exactly so the wire format
  // stays in lockstep with the existing, proven path.
  MeshHandle newHandle;
  {
    ClientMessage c(Commands::RemixApi_CreateMeshBatched);

    serializeAndSend<serialize::MeshInfo>(c, *info);

    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      switch (getSType(pNext)) {
        default:
        {
          Logger::warn("[remixapi_CreateMeshBatched] Unknown sType. Skipping.");
          break;
        }
      }
    }
    sendHandle(c, newHandle);
  }

  *out_handle = newHandle;

  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DestroyMesh(remixapi_MeshHandle handle) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DestroyMesh);
  MeshHandle meshHandle(handle);
  if(!meshHandle.isValid()) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_DestroyMesh);
    sendHandle(c, meshHandle);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DrawInstance(const remixapi_InstanceInfo* info) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DrawInstance);
  {
    ClientMessage c(Commands::RemixApi_DrawInstance);

    serializeAndSend<serialize::InstanceInfo>(c, *info);

    // For each valid pNext, we will send a true-valued bool to indicate that
    // server must read another extension. If it reads false, it knows that it
    // is done reading.
    // send(c, Bool::True); -> CONTINUE
    // send(c, Bool::False); -> STOP
    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      infoItr = pNext;
      switch (getSType(pNext)) {
        case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT:
        {
          auto* pObjectPicking = static_cast<const remixapi_InstanceInfoObjectPickingEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::InstanceInfoObjectPicking>(c, *pObjectPicking);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT:
        {
          auto* pBlend = static_cast<const remixapi_InstanceInfoBlendEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::InstanceInfoBlend>(c, *pBlend);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT:
        {
          auto* pXforms = static_cast<const remixapi_InstanceInfoBoneTransformsEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::InstanceInfoTransforms>(c, *pXforms);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_PARTICLE_SYSTEM_EXT:
        {
          auto* pParticle = static_cast<const remixapi_InstanceInfoParticleSystemEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::InstanceInfoParticleSystem>(c, *pParticle);
          break;
        }
        default:
        {
          Logger::warn("[remixapi_DrawInstance] Unknown sType. Skipping.");
          break;
        }
      }
    }
    send(c, Bool::False);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_SetupCamera(const remixapi_CameraInfo* info) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_SetupCamera);
  {
    ClientMessage c(Commands::RemixApi_SetupCamera);

    serializeAndSend<serialize::CameraInfo>(c, *info);

    // pNext chain: the parameterized-EXT camera (position/basis/fov/near/far).
    // Same true/false continuation protocol as DrawInstance.
    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      infoItr = pNext;
      switch (getSType(pNext)) {
        case REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT:
        {
          auto* pParam = static_cast<const remixapi_CameraInfoParameterizedEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::CameraInfoParameterized>(c, *pParam);
          break;
        }
        default:
        {
          Logger::warn("[remixapi_SetupCamera] Unknown sType. Skipping.");
          break;
        }
      }
    }
    send(c, Bool::False);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

// Synchronous round-trip: resolve a client D3D9 texture to its image hash on
// the server (the same XXH64 image hash Remix uses for replacement matching and
// the "0x<hash>" albedo pseudo-path). The 32-bit client has no DxvkImage, so
// the real dxvk_GetTextureHash must run server-side. Mirrors GetGameValue's
// request/Bridge_Response pattern. The uint64 hash crosses the 32-bit data
// channel as two words (lo, hi). Called once per unique texture by the wrapper
// (cached), so the blocking round-trip is not a per-frame cost.
remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_GetTextureHash(
  IDirect3DTexture9* texture,
  uint64_t*          out_hash) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_dxvk_GetTextureHash);
  if (texture == nullptr || out_hash == nullptr) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }

  // Client texture proxy -> bridge id (keys gpD3DResources server-side). Same
  // idiom as SetStreamSource / SetIndices.
  auto* const pLssTexture = bridge_cast<Direct3DTexture9_LSS*>(texture);
  const UID texId = (pLssTexture) ? (UID) pLssTexture->getId() : 0;
  if (texId == 0) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }

  UID currentUID = 0;
  {
    ClientMessage c(Commands::RemixApi_dxvk_GetTextureHash);
    currentUID = c.get_uid();
    c.send_data((uint32_t) texId);
  }
  WAIT_FOR_SERVER_RESPONSE("remixapi_dxvk_GetTextureHash", REMIXAPI_ERROR_CODE_GENERAL_FAILURE, currentUID);

  const remixapi_ErrorCode result = static_cast<remixapi_ErrorCode>(DeviceBridge::get_data());
  if (result == REMIXAPI_ERROR_CODE_SUCCESS) {
    const uint32_t lo = DeviceBridge::get_data();
    const uint32_t hi = DeviceBridge::get_data();
    *out_hash = (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
  }
  DeviceBridge::pop_front();
  return result;
}

// Walks the remixapi_LightInfo pNext extension chain and marshals each known
// *_EXT block to the server, mirroring the inline walk in remixapi_CreateLight.
// Shared by CreateLightBatched and UpdateLightDefinition so the three light
// entry points stay byte-identical on the wire. Emits the trailing Bool::False
// stop sentinel the server's pullBool() loop expects.
static void sendLightInfoExtensions(ClientMessage& c, const remixapi_LightInfo* info) {
  const void* infoItr = info;
  while (auto* const pNext = getPNext(infoItr)) {
    infoItr = pNext;
    switch (getSType(infoItr)) {
      case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT:
      {
        auto* pSphere = static_cast<const remixapi_LightInfoSphereEXT* const>(infoItr);
        send(c, Bool::True);
        serializeAndSend<serialize::LightInfoSphere>(c, *pSphere);
        break;
      }
      case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT:
      {
        auto* pRect = static_cast<const remixapi_LightInfoRectEXT* const>(infoItr);
        send(c, Bool::True);
        serializeAndSend<serialize::LightInfoRect>(c, *pRect);
        break;
      }
      case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT:
      {
        auto* pDisk = static_cast<const remixapi_LightInfoDiskEXT* const>(infoItr);
        send(c, Bool::True);
        serializeAndSend<serialize::LightInfoDisk>(c, *pDisk);
        break;
      }
      case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT:
      {
        auto* pCylinder = static_cast<const remixapi_LightInfoCylinderEXT* const>(infoItr);
        send(c, Bool::True);
        serializeAndSend<serialize::LightInfoCylinder>(c, *pCylinder);
        break;
      }
      case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT:
      {
        auto* pDistant = static_cast<const remixapi_LightInfoDistantEXT* const>(infoItr);
        send(c, Bool::True);
        serializeAndSend<serialize::LightInfoDistant>(c, *pDistant);
        break;
      }
      case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT:
      {
        auto* pDome = static_cast<const remixapi_LightInfoDomeEXT* const>(infoItr);
        send(c, Bool::True);
        serializeAndSend<serialize::LightInfoDome>(c, *pDome);
        break;
      }
      case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_USD_EXT:
      {
        auto* pUSD = static_cast<const remixapi_LightInfoUSDEXT* const>(infoItr);
        send(c, Bool::True);
        serializeAndSend<serialize::LightInfoUSD>(c, *pUSD);
        break;
      }
      default:
      {
        Logger::warn("[sendLightInfoExtensions] Unknown sType. Skipping.");
        break;
      }
    }
  }
  send(c, Bool::False);
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateLight(
  const remixapi_LightInfo* info,
  remixapi_LightHandle*     out_handle) {
    
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_CreateLight);
  assert(info->sType == REMIXAPI_STRUCT_TYPE_LIGHT_INFO);

  LightHandle newHandle;
  {
    ClientMessage c(Commands::RemixApi_CreateLight);

    serializeAndSend<serialize::LightInfo>(c, *info);
    
    // For each valid pNext, we will send a true-valued bool to indicate that
    // server must read another extension. If it reads false, it knows that it
    // is done reading.
    // send(c, Bool::True); -> CONTINUE
    // send(c, Bool::False); -> STOP
    const void* infoItr = info;
    while (auto* const pNext = getPNext(infoItr)) {
      infoItr = pNext;
      switch (getSType(infoItr)) {
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT:
        {
          auto* pSphere = static_cast<const remixapi_LightInfoSphereEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoSphere>(c, *pSphere);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT:
        {
          auto* pRect = static_cast<const remixapi_LightInfoRectEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoRect>(c, *pRect);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT:
        {
          auto* pDisk = static_cast<const remixapi_LightInfoDiskEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoDisk>(c, *pDisk);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT:
        {
          auto* pCylinder = static_cast<const remixapi_LightInfoCylinderEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoCylinder>(c, *pCylinder);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT:
        {
          auto* pDistant = static_cast<const remixapi_LightInfoDistantEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoDistant>(c, *pDistant);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT:
        {
          auto* pDome = static_cast<const remixapi_LightInfoDomeEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoDome>(c, *pDome);
          break;
        }
        case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_USD_EXT:
        {
          auto* pUSD = static_cast<const remixapi_LightInfoUSDEXT* const>(infoItr);
          send(c, Bool::True);
          serializeAndSend<serialize::LightInfoUSD>(c, *pUSD);
          break;
        }
        default:
        {
          Logger::warn("[remixapi_CreateLight] Unknown sType. Skipping.");
          break;
        }
      }
    }
    send(c, Bool::False);
    sendHandle(c, newHandle);
  }

  *out_handle = newHandle;

  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateLightBatched(
  const remixapi_LightInfo* info,
  remixapi_LightHandle*     out_handle) {

  ASSERT_REMIXAPI_PFN_TYPE(remixapi_CreateLightBatched);
  assert(info->sType == REMIXAPI_STRUCT_TYPE_LIGHT_INFO);

  // Identical wire format to remixapi_CreateLight (same serialize::LightInfo
  // payload + the shared pNext extension walk); only the server-side verb
  // differs. The renderer's CreateLightBatched defers light registration to the
  // next render-thread flush, so a 32-bit client can submit lights outside a
  // frame boundary exactly as the 64-bit path does.
  LightHandle newHandle;
  {
    ClientMessage c(Commands::RemixApi_CreateLightBatched);

    serializeAndSend<serialize::LightInfo>(c, *info);
    sendLightInfoExtensions(c, info);
    sendHandle(c, newHandle);
  }

  *out_handle = newHandle;

  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DestroyLight(remixapi_LightHandle handle) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DestroyLight);
  LightHandle lightHandle(handle);
  if(!lightHandle.isValid()) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_DestroyLight);
    sendHandle(c, lightHandle);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_DrawLightInstance(remixapi_LightHandle handle) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_DrawLightInstance);
  LightHandle lightHandle(handle);
  if(!lightHandle.isValid()) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_DrawLightInstance);
    sendHandle(c, lightHandle);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_SetConfigVariable(const char* var, const char* value) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_SetConfigVariable);
  if (!var || !value) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  {
    ClientMessage c(Commands::RemixApi_SetConfigVariable);
    send(c, var);
    send(c, value);
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_CreateD3D9(
  remixapi_Bool       editorModeEnabled,
  IDirect3D9Ex**      out_pD3D9) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_dxvk_CreateD3D9);
  Logger::err("[remixapi_dxvk_CreateD3D9] Not yet supported. Device used by Remix API defaults to "
                                         "most recently created by client application.");
  *out_pD3D9 = nullptr;
  return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
}

remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_RegisterD3D9Device(IDirect3DDevice9Ex* d3d9Device) {
  ASSERT_REMIXAPI_PFN_TYPE(remixapi_dxvk_RegisterD3D9Device);
  if (!d3d9Device) {
    return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
  }
  Logger::err("[remixapi_dxvk_RegisterD3D9Device] Not yet supported. Device used by Remix API defaults to "
                                                 "most recently created by client application.");
  return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
}

// https://stackoverflow.com/a/27490954
constexpr bool strings_equal(char const * a, char const * b) {
  return *a == *b && (*a == '\0' || strings_equal(a + 1, b + 1));
}

extern "C" {

  // Forward declarations: these two are defined later in this file (after
  // remixapi_InitializeLibrary) but are referenced in the interface-assignment
  // block below. Unlike the light/GameValue entry points, remix_c.h declares
  // only PFN typedefs for these (no free-function prototype), so without these
  // forward decls the interface assignment hits a use-before-declaration error
  // (C2065). Linkage/convention must match the definitions exactly.
  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_GetVramStats(remixapi_VramStats* out_stats);
  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_RequestVramCompaction(void);

  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_InitializeLibrary(
    const remixapi_InitializeLibraryInfo* info,
    remixapi_Interface*                   out_result) {

    static_assert(strings_equal(__func__, remixapi::exported_func_name::initRemixApi));
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_InitializeLibrary);
    
    if (!GlobalOptions::getExposeRemixApi()) {
      Logger::err("Remix API is not enabled. This is currently an experimental feature and must be explicitly enabled \
                   in the `bridge.conf`. Please set `exposeRemixApi = True` if you are sure you want it enabled.");
      return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
    }
    if (!info || info->sType != REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    if (!out_result) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    auto interf = remixapi_Interface {};
    {
      // interf.Startup = remixapi_Startup;
      // interf.Shutdown = remixapi_Shutdown;
      // interf.Present = remixapi_Present;
      interf.CreateMaterial = remixapi_CreateMaterial;
      interf.DestroyMaterial = remixapi_DestroyMaterial;
      interf.CreateMesh = remixapi_CreateMesh;
      interf.CreateMeshBatched = remixapi_CreateMeshBatched;
      interf.DestroyMesh = remixapi_DestroyMesh;
      interf.SetupCamera = remixapi_SetupCamera;
      interf.DrawInstance = remixapi_DrawInstance;
      interf.CreateLight = remixapi_CreateLight;
      interf.CreateLightBatched = remixapi_CreateLightBatched;
      interf.DestroyLight = remixapi_DestroyLight;
      interf.DrawLightInstance = remixapi_DrawLightInstance;
      interf.SetConfigVariable = remixapi_SetConfigVariable;
      interf.dxvk_CreateD3D9 = remixapi_dxvk_CreateD3D9;
      interf.dxvk_RegisterD3D9Device = remixapi_dxvk_RegisterD3D9Device;
      // Texture-hash lookup over the bridge: lets external API materials bind a
      // captured vanilla texture by its image hash (Morrowind distant-statics
      // batching, Tier 1 materials). Forwarded synchronously to the server.
      interf.dxvk_GetTextureHash = remixapi_dxvk_GetTextureHash;
      // Fork-added Remix API entry points. dxvk-remix's d3d9.dll implements
      // these for real (rtx_remix_api.cpp:2244+ / 2357+ / 2409+ / 2413+).
      // GetUIState / SetUIState remain client-side stubs (UI state is not
      // meaningful across the bridge yet); AutoInstancePersistentLights and
      // UpdateLightDefinition are now fully forwarded over IPC (see their
      // marshallers below) so 32-bit clients can flush persistent lights and
      // update analytical lights created via CreateLight / CreateLightBatched.
      interf.GetUIState                   = remixapi_GetUIState;
      interf.SetUIState                   = remixapi_SetUIState;
      interf.AutoInstancePersistentLights = remixapi_AutoInstancePersistentLights;
      interf.UpdateLightDefinition        = remixapi_UpdateLightDefinition;
      interf.SetGameValue                 = remixapi_SetGameValue;
      interf.GetGameValue                 = remixapi_GetGameValue;
      // VRAM telemetry + compaction (fork hooks). GetVramStats backs MegaGeo's
      // adaptive-LOD VRAM signal; RequestVramCompaction releases retained
      // chunks at bulk scene-turnover. Both forwarded over the bridge.
      interf.GetVramStats                 = remixapi_GetVramStats;
      interf.RequestVramCompaction        = remixapi_RequestVramCompaction;
      // interf.dxvk_GetExternalSwapchain = remixapi_dxvk_GetExternalSwapchain;
      // interf.dxvk_GetVkImage = remixapi_dxvk_GetVkImage;
      // interf.dxvk_CopyRenderingOutput = remixapi_dxvk_CopyRenderingOutput;
      // interf.dxvk_SetDefaultOutput = remixapi_dxvk_SetDefaultOutput;
      // interf.pick_RequestObjectPicking = remixapi_pick_RequestObjectPicking;
      // interf.pick_HighlightObjects = remixapi_pick_HighlightObjects;
    }

    *out_result = interf;
    remixapi::g_bInterfaceInitialized = true;

    return REMIXAPI_ERROR_CODE_SUCCESS;
  }
  
  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_RegisterCallbacks(
    PFN_remixapi_BridgeCallback beginSceneCallback,
    PFN_remixapi_BridgeCallback endSceneCallback,
    PFN_remixapi_BridgeCallback presentCallback) {

    static_assert(strings_equal(__func__, remixapi::exported_func_name::registerCallbacks));
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_RegisterCallbacks);

    remixapi::g_beginSceneCallback = beginSceneCallback;
    remixapi::g_endSceneCallback = endSceneCallback;
    remixapi::g_presentCallback = presentCallback;
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  // ---- Fork-added Remix API entry points (client-side stubs) ----
  // Mirror the surface dxvk-remix exposes (rtx_remix_api.cpp ~2244, ~2357,
  // ~2409, ~2413) so the function pointers in remixapi_Interface aren't NULL
  // for 32-bit clients. These stubs do NOT forward over IPC yet — full
  // plumbing is a follow-up. Each logs once the first time it's called so
  // the user sees that the feature isn't reachable through the bridge.

  DLLEXPORT remixapi_UIState __stdcall remixapi_GetUIState(void) {
    static bool warned = false;
    if (!warned) {
      Logger::warn("[remixapi_GetUIState] Bridge stub: returning REMIXAPI_UI_STATE_NONE. "
                   "Remix UI state queries are not yet plumbed through the bridge.");
      warned = true;
    }
    return REMIXAPI_UI_STATE_NONE;
  }

  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_SetUIState(remixapi_UIState state) {
    (void) state;
    static bool warned = false;
    if (!warned) {
      Logger::warn("[remixapi_SetUIState] Bridge stub: no-op. "
                   "Remix UI state changes are not yet plumbed through the bridge.");
      warned = true;
    }
    return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
  }

  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_AutoInstancePersistentLights(void) {
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_AutoInstancePersistentLights);
    // No payload — a bare command that asks the server to flush/auto-instance
    // any persistent API lights for the frame. Fire-and-forget, matching the
    // non-waiting light commands (CreateLight / DrawLightInstance).
    {
      ClientMessage c(Commands::RemixApi_AutoInstancePersistentLights);
    }
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_UpdateLightDefinition(
    remixapi_LightHandle      handle,
    const remixapi_LightInfo* info) {
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_UpdateLightDefinition);
    if (info == nullptr || info->sType != REMIXAPI_STRUCT_TYPE_LIGHT_INFO) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    LightHandle lightHandle(handle);
    if (!lightHandle.isValid()) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    // Same LightInfo + extension-chain wire format as CreateLight, with the
    // existing light handle appended so the server can resolve and update it.
    // The renderer queues the update and applies it safely on a later frame, so
    // this is fire-and-forget (no server response wait), matching DrawLightInstance.
    {
      ClientMessage c(Commands::RemixApi_UpdateLightDefinition);
      serializeAndSend<serialize::LightInfo>(c, *info);
      sendLightInfoExtensions(c, info);
      sendHandle(c, lightHandle);
    }
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_SetGameValue(
    const char* key,
    const char* value) {
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_SetGameValue);
    if (key == nullptr || key[0] == '\0') {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    if (value == nullptr) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    UID currentUID = 0;
    {
      ClientMessage c(Commands::RemixApi_SetGameValue);
      currentUID = c.get_uid();
      send(c, key);
      send(c, value);
    }
    WAIT_FOR_SERVER_RESPONSE("remixapi_SetGameValue", REMIXAPI_ERROR_CODE_GENERAL_FAILURE, currentUID);
    const remixapi_ErrorCode result = static_cast<remixapi_ErrorCode>(DeviceBridge::get_data());
    DeviceBridge::pop_front();
    return result;
  }

  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_GetGameValue(
    const char* key,
    char*       out_buffer,
    uint32_t    in_buffer_size,
    uint32_t*   out_actual_size) {
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_GetGameValue);
    if (key == nullptr || key[0] == '\0') {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    if (out_actual_size == nullptr) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    if (in_buffer_size > 0 && out_buffer == nullptr) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    UID currentUID = 0;
    {
      ClientMessage c(Commands::RemixApi_GetGameValue);
      currentUID = c.get_uid();
      send(c, key);
      c.send_data(in_buffer_size);
    }
    WAIT_FOR_SERVER_RESPONSE("remixapi_GetGameValue", REMIXAPI_ERROR_CODE_GENERAL_FAILURE, currentUID);
    const remixapi_ErrorCode result = static_cast<remixapi_ErrorCode>(DeviceBridge::get_data());
    const uint32_t actual = DeviceBridge::get_data();
    *out_actual_size = actual;
    if (actual > 0 && in_buffer_size >= actual) {
      // Server sent the value bytes when the caller's buffer was large enough.
      void* value_ptr = nullptr;
      const uint32_t value_size = DeviceBridge::get_data(&value_ptr);
      (void) value_size;
      memcpy(out_buffer, value_ptr, actual);
    }
    DeviceBridge::pop_front();
    return result;
  }

  // Force the DXVK allocator to release retained empty chunks back to the
  // driver on the next render-thread tick. Fire-and-forget — no payload, no
  // response wait (matches AutoInstancePersistentLights). The renderer sets an
  // atomic flag; nothing to read back.
  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_RequestVramCompaction(void) {
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_RequestVramCompaction);
    {
      ClientMessage c(Commands::RemixApi_RequestVramCompaction);
    }
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  // Request the per-category VRAM snapshot. remixapi_VramStats is a flat POD
  // (10x uint64 + 1x uint32, no pointers), so the server fills one and ships
  // the raw bytes back over a single Bridge_Response, mirroring GetGameValue's
  // request/response round-trip. MegaGeo's adaptive-LOD controller polls this
  // (usedAccelerationStructureBytes) to decide back-off; without it the LOD
  // path runs degraded (no VRAM signal).
  DLLEXPORT remixapi_ErrorCode __stdcall remixapi_GetVramStats(
    remixapi_VramStats* out_stats) {
    ASSERT_REMIXAPI_PFN_TYPE(remixapi_GetVramStats);
    if (out_stats == nullptr) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    UID currentUID = 0;
    {
      ClientMessage c(Commands::RemixApi_GetVramStats);
      currentUID = c.get_uid();
    }
    WAIT_FOR_SERVER_RESPONSE("remixapi_GetVramStats", REMIXAPI_ERROR_CODE_GENERAL_FAILURE, currentUID);
    const remixapi_ErrorCode result = static_cast<remixapi_ErrorCode>(DeviceBridge::get_data());
    if (result == REMIXAPI_ERROR_CODE_SUCCESS) {
      void* stats_ptr = nullptr;
      const uint32_t stats_size = DeviceBridge::get_data(&stats_ptr);
      // Guard against a truncated/short payload before copying into the caller's
      // struct; on a size mismatch surface failure rather than read past the
      // received bytes.
      if (stats_ptr != nullptr && stats_size == sizeof(remixapi_VramStats)) {
        memcpy(out_stats, stats_ptr, sizeof(remixapi_VramStats));
      } else {
        DeviceBridge::pop_front();
        return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
      }
    }
    DeviceBridge::pop_front();
    return result;
  }

}

}
