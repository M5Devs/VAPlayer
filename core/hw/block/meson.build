system_ss.add(files(
  'block.c',
  'cdrom.c',
  'hd-geometry.c'
))
system_ss.add(when: 'CONFIG_FDC', if_true: files('fdc.c'))
system_ss.add(when: 'CONFIG_FDC_ISA', if_true: files('fdc-isa.c'))
system_ss.add(when: 'CONFIG_PFLASH_CFI01', if_true: files('pflash_cfi01.c'))

system_ss.add(when: 'CONFIG_VIRTIO_BLK', if_true: files('virtio-blk.c'))
system_ss.add(when: 'CONFIG_VIRTIO_BLK', if_true: files('virtio-blk-common.c'))

subdir('dataplane')
