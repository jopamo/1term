project('myproject', 'c',
  version: '0.1.0',

  default_options : [
    'c_std=c2x',          # keep the language level
    'optimization=3',     # -O3   (Meson levels: 0/g/1/2/3/s/z)
    'b_lto=true',         # -flto (link-time optimisation, thin LTO if Clang)
    'b_pie=true',         # position-independent executable
    'b_asneeded=true',    # drop unused shared-libraries at link time
    'b_staticpic=false',  # don’t force PIC into static libs we don’t build
    'b_ndebug=if-release' # strip assertions only for release builds
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
# ────────────────────────────────────────────
gtk_dep  = dependency('gtk4')
vte_dep  = dependency('vte-2.91-gtk4')
glib_dep = dependency('glib-2.0')
zstd_dep = dependency('libzstd')

# ────────────────────────────────────────────
#  Targets
# ────────────────────────────────────────────
exe_1term = executable('1term',
  '1term.c',
  dependencies : [gtk_dep, vte_dep, glib_dep, zstd_dep],
  install      : true
)

# ────────────────────────────────────────────
#  Desktop integration
# ────────────────────────────────────────────
install_data('1term.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'))
install_data('1term.png',
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor',
                          '256x256', 'apps'))
