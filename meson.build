project('myproject', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11'
  ]
)

gtk_dep = dependency('gtk4')
vte_dep = dependency('vte-2.91-gtk4')

exe_1term = executable('1term',
  '1term.c',
  dependencies: [gtk_dep, vte_dep],
  install: true
)

install_data('1term.desktop', install_dir: join_paths(get_option('prefix'), 'share/applications'))
install_data('1term.png', install_dir: join_paths(get_option('prefix'), 'share/icons/hicolor/256x256/apps'))
