project('rcnode', 'c',
  version : '0.1',
  default_options : ['warning_level=3', 'c_std=c99']
)

cc = meson.get_compiler('c')

# --- Dependencies ---
sqlite3_dep   = dependency('sqlite3')
crypto_dep    = cc.find_library('crypto', required: true)
dl_dep        = cc.find_library('dl', required: true)
vulkan_dep    = dependency('vulkan', required: true)

deps = [sqlite3_dep, crypto_dep, dl_dep, vulkan_dep]

# --- Sources ---
rcnode_sources = files(
  'src/main.c',
  'src/eval_util.c',
  'src/rewrite_util.c',
  'src/string_set.c',
  'src/eval.c',
  'src/spirv.c',
  'src/sexpr_parser_util.c',
  'src/sexpr_parser.c',
  'src/spirv_asm.c',
  'src/spirv_passes.c',
  'src/util.c',
  'src/compiler.c',
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
  include_directories: include_directories('src', 'external', 'external/mkrand',
                                           'external/tinyosc', 'external/cJSON'),
  dependencies: deps,
  install: true
)

# --- Validator ---
validate_sources = files(
  'src/validate_sexpr.c',
  'src/compiler.c',
  'src/sexpr_parser_util.c',
  'src/sexpr_parser.c',
  'src/string_set.c',
  'src/eval_util.c',
  'src/rewrite_util.c',
  'src/eval.c',
  'src/log.c',
  'src/spirv.c',
  'src/spirv_asm.c',
  'src/spirv_passes.c',
  'src/wiring.c'
)

validate_bin = executable('validate_sexpr',
  validate_sources,
  include_directories: include_directories('src', 'external'),
  dependencies: deps
)

# --- Output directories ---
out_root = join_paths(meson.current_build_dir(), 'out')
out_dirs = [
  join_paths(out_root, 'sexpr'),
  join_paths(out_root, 'spirv_sexpr'),
  join_paths(out_root, 'spirv_unified'),
  join_paths(out_root, 'spirv_compiled'),
  join_paths(out_root, 'state')
]

# Create out directories at configure time
foreach d : out_dirs
  run_command('mkdir', '-p', d, check: true)
endforeach

# --- Compilation step ---
compile_output = custom_target('compile-all',
  output: 'main.spvasm.sexpr', 
  command: [rcnode_bin, '--compile',
            '--inv', join_paths(meson.source_root(), 'inv'),
            '--output', out_root],
  build_by_default: true
)


# --- SPIR-V binary ---
spirv_asm_path = join_paths(out_root, 'spirv_unified', 'main.spvasm')
spirv_bin_path = join_paths(out_root, 'spirv_compiled', 'main.spv')
spirv_compiled_file = join_paths(out_root, 'spirv_compiled', 'main.spv')

spirv_asm_file = join_paths(out_root, 'spirv_asm', 'main.spvasm')
spirv_bin = custom_target('spirv-bin',
  input: compile_output,
  output: 'main.spv',
  command: ['spirv-as', spirv_asm_file, '-o', spirv_compiled_file],
  build_by_default: true
)

custom_target('clean_outdir',
  output: 'clean.out',
  command: [
    'sh', '-c',
    'rm -rf out/sexpr/* out/spirv_*/* out/graph.json && touch out/clean.out'
  ],
  build_by_default: true
)


