scsi_ss = ss.source_set()
virtio_scsi_ss = ss.source_set()

scsi_ss.add(files(
  'emulation.c',
  'scsi-bus.c',
  'scsi-disk.c',
  'scsi-generic.c',
))

virtio_scsi_ss.add(files('virtio-scsi-dataplane.c'))
virtio_scsi_ss.add(when: 'CONFIG_VHOST_SCSI', if_true: files('vhost-scsi.c'))
virtio_scsi_ss.add(when: 'CONFIG_VHOST_USER_SCSI', if_true: files('vhost-user-scsi.c'))

virtio_scsi_ss.add(files('virtio-scsi.c'))
virtio_scsi_ss.add(when: 'CONFIG_VHOST_SCSI_COMMON', if_true: files('vhost-scsi-common.c'))

scsi_ss.add_all(when: 'CONFIG_VIRTIO_SCSI', if_true: virtio_scsi_ss)

system_ss.add_all(when: 'CONFIG_SCSI', if_true: scsi_ss)
