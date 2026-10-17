project('libmorsedec', 'c', version: '0.2.0', license: 'GPL-3.0-or-later',
  meson_version: '>=0.63.0', default_options: ['c_std=c11', 'warning_level=3'])
cc = meson.get_compiler('c')
fftw = dependency('fftw3', required: false)
if not fftw.found()
  fftw = declare_dependency(dependencies: cc.find_library('fftw3'),
    compile_args: [], include_directories: include_directories('.'))
endif
math = cc.find_library('m', required: false)
inc = include_directories('include', 'src')
sources = files('src/decoder.c', 'src/frontend.c', 'src/band.c', 'src/signal.c',
  'src/segment.c', 'src/statistics.c', 'src/character.c', 'src/word.c', 'src/message.c')
cargs = ['-DMORSEDEC_BUILD']
if get_option('default_library') == 'static'
  cargs += '-DMORSEDEC_STATIC'
endif
lib = library('morsedec', sources, include_directories: inc,
  dependencies: [fftw, math], c_args: cargs, gnu_symbol_visibility: 'hidden',
  version: meson.project_version(), soversion: '1', install: true)
consumer_args = []
if get_option('default_library') == 'static'
  consumer_args += '-DMORSEDEC_STATIC'
endif
morsedec_dep = declare_dependency(link_with: lib, include_directories: include_directories('include'),
  compile_args: consumer_args)
install_headers('include/morsedec.h')
subdir('docs/man')
pkg = import('pkgconfig')
pkg.generate(lib, name: 'libmorsedec', filebase: 'morsedec',
  description: 'Streaming multichannel amateur radio CW decoder',
  extra_cflags: consumer_args, requires_private: 'fftw3', libraries_private: math)
sndfile = dependency('sndfile', required: get_option('tools'))
if sndfile.found() and not get_option('tools').disabled()
  cli = executable('morsedec-audio', ['tools/morsedec-audio.c', 'tools/audio-common.c'],
    dependencies: [morsedec_dep, sndfile, math], install: true)
endif
if not get_option('tools').disabled()
  live_sources = ['tools/live-common.c', 'tools/audio-common.c']
  live = executable('morsedec-live', ['tools/morsedec-live.c'] + live_sources,
    dependencies: [morsedec_dep, math], install: true)
  iqlive = executable('morsedec-iqlive', ['tools/morsedec-iqlive.c'] + live_sources,
    dependencies: [morsedec_dep, math], install: true)
endif
example = executable('morsedec-example', 'examples/decode.c', dependencies: morsedec_dep)
formatter = find_program('clang-format', required: false)
if formatter.found()
  format_files = sources + files('include/morsedec.h', 'src/internal.h', 'src/codes.h', 'src/templates.h',
    'tools/morsedec-audio.c', 'tools/morsedec-live.c', 'tools/morsedec-iqlive.c',
    'tools/live-common.c', 'tools/audio-common.c',
    'tools/audio-common.h', 'examples/decode.c')
  run_target('format', command: [formatter, '-i', format_files])
  run_target('format-check', command: [formatter, '--dry-run', '--Werror', format_files])
endif
install_data('COPYING', install_dir: get_option('datadir') / 'licenses' / 'libmorsedec')
