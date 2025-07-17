project('verilog_r30_cell')

iverilog_bin = find_program('iverilog', required: true)
vvp_bin = find_program('vvp', required: true)

verilog_sources = files(
  'r30_cell.v',
  'r30_array.v',
  'r30_field.v',
  'r30_entropy_gen.v',
  'r30_entropy_gen.tb.v'  # 👈 New testbench
)

# Convert to full paths
verilog_paths = []
foreach f : verilog_sources
  verilog_paths += f.full_path()
endforeach

# Ensure output dir exists
run_command('mkdir', '-p', 'out/gtkwave', check: true)

verilog_sim = custom_target('r30_entropy_sim',
  input: verilog_sources,
  output: 'entropy_wave.vcd',
  command: [
    'sh', '-c',
    iverilog_bin.full_path() + ' -o r30_entropy_sim.out ' +
    ' '.join(verilog_paths) + ' && ' +
    vvp_bin.full_path() + ' r30_entropy_sim.out' 
  ],
  build_by_default: true
)

# Optional GTKWave viewer
gtkwave = find_program('gtkwave', required: false)

if gtkwave.found()
  custom_target('view_entropy_wave',
    input: verilog_sim,
    output: 'waveview.txt',
    command: [
      'sh', '-c',
      'gtkwave entropy_wave.vcd && touch waveview.txt'
    ]
  )
endif
