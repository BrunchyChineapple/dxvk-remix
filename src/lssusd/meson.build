lssUsd_src = files([
  'game_exporter.cpp',
  'usd_mesh_importer.cpp',
  'usd_mesh_importer.h',
  'usd_mesh_samplers.h',
  'hd/usd_mesh_util.cpp',
  'hd/usd_mesh_util.h',
  'mdl_helpers.h'
])

if not dxvk_is_msvc
  lss_Usd_args = ['-fpermissive']
else
  lss_Usd_args = []
endif
thread_dep = dependency('threads')

# Embed the MDLs the capture exporter writes into captures. These were repointed
# from the build-time-fetched `external/omni_core_materials/Base/` package to the
# in-repo `src/mdl/Base/` copies (migrated by 1ea51ea0) so the C255 bool-range
# warning fix (86f4f5b74) actually reaches the binary -- the external package copy
# is git-ignored, fetched stale, and still carries the buggy
# `anno::hard_range(0, 1)` on `subsurface_diffusion_profile`.
libusd_embedded_data_files = files([
  '../mdl/Base/AperturePBR_Opacity.mdl',
  '../mdl/Base/AperturePBR_Translucent.mdl',
  '../mdl/Base/AperturePBR_Model.mdl',
  '../mdl/Base/AperturePBR_Normal.mdl',
  '../mdl/Base/AperturePBR_SpriteSheet.mdl',
])

libusd_generated_embedded_files = embedding_compiler.process(libusd_embedded_data_files)

lssUsd_lib = static_library('lssUsd', lssUsd_src,[libusd_generated_embedded_files],
  dependencies        : [ usd_dep, embedded_dep ],
  include_directories : [ usd_include_paths, lssusd_include_paths ],
  override_options    : ['cpp_std='+dxvk_cpp_std],
  cpp_args            : lss_Usd_args)

lssUsd_dep = declare_dependency(
  link_with           : [ lssUsd_lib ])
