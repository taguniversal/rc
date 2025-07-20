define(`PSIplus', `
`'define(`n', `$1')dnl
`'ifdef(`PSI_`'n`'_defined', `', `
  define(`PSI_0_defined')dnl
  wire [127:0] PSI_0 = INITIAL_PSI;

  ifelse(n, 1, `',
    forloop(`i', 1, n, `
      define(`PSI_`'i`'_defined')dnl
      wire [127:0] PSI_`'i;
      R30_EntropyGen step`'i (.seed(PSI_`'decr(i)), .out(PSI_`'i));
    ')
  )
')
PSI_`'n
')dnl
