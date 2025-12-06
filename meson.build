project('1term', 'c',
  version: '0.1.0',

  default_options : [
    'c_std=c2x',
    'optimization=3',
    'b_lto=true',
    'b_pie=true',
    'b_asneeded=true',
    'b_staticpic=false',
    'b_ndebug=if-release'
  ]
)

cc = meson.get_compiler('c')

opt_c_args = [
  '-fno-plt',
  '-fdata-sections',
  '-ffunction-sections',
  '-fvisibility=hidden'
]

opt_link_args = [
  '-Wl,--gc-sections',
]

if get_option('optimization') != '0'
  add_project_arguments(opt_c_args, language : 'c')
  add_project_link_arguments(opt_link_args, language : 'c')
endif


# ────────────────────────────────────────────
#  External dependencies
#  Ensure modern GTK4 (>=4.14) and VTE (>=0.70) for native drawing primitives
#  and frame-clock driven rendering (reduces latency, improves performance)
# ────────────────────────────────────────────
gtk_dep  = dependency('gtk4', version: '>=4.14')
vte_dep  = dependency('vte-2.91-gtk4', version: '>=0.70')
glib_dep = dependency('glib-2.0')
zstd_dep = dependency('libzstd')

# ────────────────────────────────────────────
#  Targets
# ────────────────────────────────────────────
exe_1term = executable('1term',
  ['src/main.c', 'src/window.c', 'src/tab.c', 'src/terminal.c', 'src/clipboard.c'],
  dependencies : [gtk_dep, vte_dep, glib_dep, zstd_dep],
  install      : true
)

# ────────────────────────────────────────────
#  Desktop integration
# ────────────────────────────────────────────
install_data('assets/desktop/1term.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'))
install_data('assets/icons/1term.png',
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor',
                          '256x256', 'apps'))
install_data('assets/icons/1term.svg',
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor',
                          'scalable', 'apps'))
