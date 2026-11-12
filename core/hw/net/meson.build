system_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('net_rx_pkt.c'))
system_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('virtio-net.c'))

if have_vhost_net
  system_ss.add(when: 'CONFIG_VIRTIO_NET', if_true: files('vhost_net.c'))
endif
stub_ss.add(files('vhost_net-stub.c'))
