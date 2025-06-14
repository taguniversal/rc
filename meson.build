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
zeromq_dep   = dependency('libzmq', required: true)
czmq_dep = dependency('libczmq', required: true)
deps = [sqlite3_dep, crypto_dep, dl_dep, vulkan_dep, zeromq_dep, czmq_dep]

# --- Sources ---
rcnode_sources = files(
  'src/main.c',
  'src/block.c',
  'src/block_util.c',
  'src/eval_util.c',
  'src/rewrite_util.c',
  'src/string_list.c',
  'src/pubsub.c',
  'src/gap.c',
  'src/eval.c',
  'src/emit_spirv.c',
  'src/emit_sexpr.c',
  'src/emit_util.c',
  'src/instance.c',
  'src/invocation.c',
  'src/sexpr_parser_util.c',
  'src/sexpr_parser.c',
  'src/spirv_asm.c',
  'src/spirv_passes.c',
  'src/util.c',
  'src/compiler.c',
  'src/log.c',
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


# --- Output directories ---
out_root = join_paths(meson.current_build_dir(), 'out')
out_dirs = [
  join_paths(out_root, 'stage')
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
spirv_stage5_dir = join_paths(meson.current_build_dir(), 'out/stage/5/spirv')
spirv_input = join_paths(spirv_stage5_dir, 'main.spvasm')
spirv_output = join_paths(meson.current_build_dir(), 'out/spirv_compiled/main.spv')

spirv_bin = custom_target('spirv-bin',
  input : spirv_input,
  output : 'main.spv',
  command : ['/usr/local/bin/spirv-as', '@INPUT@', '-o', '@OUTPUT@'],
  install : false
)


