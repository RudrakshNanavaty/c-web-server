project(
  'web-server',
  'c',
  meson_version : '>= 1.3.0',
  version : '0.1',
  default_options : ['warning_level=3'],
)

dependencies = [
]

sources = [
  'src/main.c',
  'src/server.c',
]

exe = executable(
  'web-server',
  sources,
  dependencies : dependencies,
  install : true,
)

run_target('run', command: [exe])

test('basic', exe)
