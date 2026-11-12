system_ss.add(when: 'CONFIG_APM', if_true: files('apm.c'))
system_ss.add(when: 'CONFIG_I82378', if_true: files('i82378.c'))
system_ss.add(when: 'CONFIG_ISA_BUS', if_true: files('isa-bus.c'))
system_ss.add(when: 'CONFIG_ISA_SUPERIO', if_true: files('isa-superio.c'))
system_ss.add(when: 'CONFIG_PIIX', if_true: files('piix.c'))

specific_ss.add(when: 'CONFIG_LPC_ICH9', if_true: files('lpc_ich9.c'))
