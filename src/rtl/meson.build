project('verilog_r30_cell')

iverilog_bin = find_program('iverilog')
vvp_bin = find_program('vvp')
m4 = find_program('m4')

m4_sources = [
  'r30_cell.v.m4',
  'r30_array.v.m4',
  'r30_field.v.m4',
  'r30_entropy_gen.v.m4'
]
verilog_expanded = []
verilog_expanded_names = []

foreach src_fname : m4_sources
  base = src_fname.split('.v.m4')[0]
  out_v = base + '.v'

  ct = custom_target(base + '_expanded',
    input: src_fname,
    output: out_v,
    command: [m4, '@INPUT@'],
    capture: true
  )

  verilog_expanded += ct
  verilog_expanded_names += ct  # Use the CustomTarget for full_path()
endforeach

verilog_tb = files('r30_entropy_gen.tb.v')
verilog_tb_path = verilog_tb[0].full_path()

verilog_all_paths = []
foreach tgt : verilog_expanded
  verilog_all_paths += tgt.full_path()
endforeach
verilog_all_paths += verilog_tb_path

verilog_sim = custom_target('r30_entropy_sim',
  input: verilog_expanded + verilog_tb,
  output: 'entropy_wave.vcd',
  command: [
    'sh', '-c',
    iverilog_bin.full_path() + ' -o r30_entropy_sim.out ' +
    ' '.join(verilog_all_paths) + ' && ' +
    vvp_bin.full_path() + ' r30_entropy_sim.out'
  ],
  build_by_default: true
)
