subdir('gtkpp')

nvim_ui_lib_sources = [
  'config.cpp',
  'config.hpp',
  'AsyncExec.cpp',
  'AsyncExec.hpp',
  'GridLine.hpp',
  'Input.cpp',
  'Input.hpp',
  'IWindow.hpp',
  'Logger.cpp',
  'Logger.hpp',
  'MsgPackRpc.cpp',
  'MsgPackRpc.hpp',
  'RedrawHandler.cpp',
  'RedrawHandler.hpp',
  'Renderer.cpp',
  'Renderer.hpp',
  'Session.cpp',
  'Session.hpp',
  'SessionSpawn.cpp',
  'SessionSpawn.hpp',
  'SessionTcp.cpp',
  'SessionTcp.hpp',
  'Timer.cpp',
  'Timer.hpp',
  'UvLoop.cpp',
  'UvLoop.hpp',
]

nvim_ui_lib = static_library('nvim-ui-lib',
  nvim_ui_lib_sources,
  dependencies: [spdlog_dep, msgpack_cxx_dep, libuv_dep],
  cpp_pch: 'pch/all.hpp',
)

nvim_ui_lib_dep = declare_dependency(
  include_directories: '.',
)

nvim_ui_sources = [
  'GCallbackAdaptor.hpp',
  'GConfig.hpp',
  'GConfig.cpp',
  'GCursor.cpp',
  'GCursor.hpp',
  'GFont.cpp',
  'GFont.hpp',
  'GGrid.cpp',
  'GGrid.hpp',
  'GSettingsDlg.hpp',
  'GSettingsDlg.cpp',
  'GWindow.cpp',
  'GWindow.hpp',
  'IWindowHandler.hpp',
  'main.cpp',
  gtk_res,
  win_res,
]

executable('nvim-ui',
  nvim_ui_sources,
  dependencies: [spdlog_dep, msgpack_cxx_dep, gtk4_dep, gtkpp_dep],
  link_with: [nvim_ui_lib],
  cpp_pch: 'pch/all-gtk.hpp',
  win_subsystem: 'windows',
  cpp_args: '-DGIR_INLINE',
  install: true)
