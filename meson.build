project('rcnode', 'c',
  version : '0.1',
  default_options : ['warning_level=3', 'c_std=c99']
)

cc = meson.get_compiler('c')

# --- Dependencies ---
sqlite3_dep   = dependency('sqlite3')
xml2_dep      = dependency('libxml-2.0')
crypto_dep    = cc.find_library('crypto', required: true)
dl_dep        = cc.find_library('dl', required: true)
pthread_dep   = dependency('threads')
vulkan_dep    = dependency('vulkan', required: true)

deps = [sqlite3_dep, xml2_dep, crypto_dep, dl_dep, pthread_dep, vulkan_dep]

# --- Sources ---
rcnode_sources = files(
  'src/main.c',
  'src/eval.c',
  'src/spirv.c',
  'src/sexpr_parser.c',
  'src/spirv_asm.c',
  'src/util.c',
  'src/log.c',
  'src/graph.c',
  'src/vulkan_driver.c',
  'src/wiring.c',
  'src/udp_send.c',
  'src/osc.c'
)

external_sources = files(
  'external/cJSON/cJSON.c',
  'external/mkrand/mkrand.c',
  'external/tinyosc/tinyosc.c'
)

rcnode_bin = executable('rcnode',
  rcnode_sources + external_sources,
  include_directories: include_directories('src', 'external','external/mkrand',
    'external/tinyosc', 'external/cJSON'),
  dependencies: deps,
  install: true
)

# --- Validator (optional) ---
validate_sources = files(
  'src/validate_sexpr.c',
  'src/sexpr_parser.c',
  'src/eval.c',
  'src/log.c'
)

validate_bin = executable('validate_sexpr',
  validate_sources,
  include_directories: include_directories('src', 'external', 'external/tinyosc', 'external/mkrand', 'external/sqlite'),
  dependencies: deps
)

# --- Output directory root ---
output_dir = 'build/out'

# --- Compilation step that emits all files ---
compile_output = custom_target('compile-all',
  output: ['main.sexpr', 'main.spvasm.sexpr', 'main.spvasm'],
  command: [rcnode_bin, '--compile',
            '--inv', join_paths(meson.source_root(), 'inv'),
            '--output', meson.current_build_dir()],
  build_by_default: true
)


# --- SPIR-V binary (from main.spvasm) ---
spirv_bin = custom_target('spirv-bin',
  input: compile_output,
  output: 'main.spv',
  command: ['spirv-as', output_dir + '/spirv_asm/main.spvasm', '-o', '@OUTPUT@']
)
