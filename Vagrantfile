Vagrant.configure("2") do |config|

  # OS to use for the VM
  config.vm.box = "ubuntu/artful64"

  config.vm.network "private_network", type: "dhcp", auto_config: false
  config.vm.network "private_network", type: "dhcp", auto_config: false

  config.vm.provision "file", source: "/etc/letsencrypt/live/slate.eggert.org",
    destination: "~/slate.eggert.org"

  # hardware configuration of the VM
  config.vm.provider "virtualbox" do |vb|
    vb.gui = false
    vb.memory = 4096
    vb.cpus = 2
    vb.linked_clone = true
    vb.name = config.vm.hostname

    # tweak the hardware
    vb.customize ["modifyvm", :id, "--chipset", "ich9"]
    vb.customize ["modifyvm", :id, "--hpet", "on"]
    vb.customize ["modifyvm", :id, "--ioapic", "on"]
    vb.customize ["modifyvm", :id, "--x2apic", "on"]
    vb.customize ["modifyvm", :id, "--largepages", "on"]
    vb.customize ["setextradata", :id, "VBoxInternal/CPUM/SSE4.1", "1"]
    vb.customize ["setextradata", :id, "VBoxInternal/CPUM/SSE4.2", "1"]

    # use virtio for uplink, other NIC types for experimentation interfaces
    vb.customize ["modifyvm", :id, "--nictype1", "virtio"]
    vb.customize ["modifyvm", :id, "--nictype2", "82545EM"]
    vb.customize ["modifyvm", :id, "--nictype3", "82545EM"]

    # better clock synchronization (to within 100ms)
    vb.customize [ "guestproperty", "set", :id,
      "/VirtualBox/GuestAdd/VBoxService/--timesync-set-threshold", 100 ]
  end

  # set up cache, if plugin exists
  if Vagrant.has_plugin?("vagrant-cachier")
    config.cache.scope = :box
    config.cache.synced_folder_opts = { owner: "_apt" }
  end

  # resize fs, if plugin exists
  if Vagrant.has_plugin?("vagrant-disksize")
    config.vm.provision "shell", inline: "resize2fs /dev/sda1"
  end

  # apply some fixes to the VM OS, update it, and install some tools
  config.vm.provision "shell", inline: <<-SHELL
    export DEBIAN_FRONTEND=noninteractive

    git config --global user.email lars@netapp.com
    git config --global user.name "Lars Eggert"

    # update the box
    apt-get -y update
    apt-get -y upgrade

    # install some tools that are needed
    apt-get -y install git cmake ninja-build libev-dev libssl-dev g++ \
      libhttp-parser-dev libbsd-dev pkg-config mercurial dpdk dpdk-dev \
      python-pyelftools dpdk-igb-uio-dkms dpdk-rte-kni-dkms

    # install some tools that are useful
    apt-get -y install tmux fish gdb htop silversearcher-ag valgrind hugepages

    # change shell to fish
    chsh -s /usr/bin/fish vagrant

    # get Linux kernel sources, for building netmap
    apt-get source linux-image-$(uname -r)

    # compile and install netmap
    git clone https://github.com/luigirizzo/netmap
    cd netmap
    ./configure --driver-suffix=-netmap --no-ext-drivers \
      --kernel-sources=/home/vagrant/linux-$(uname -r | cut -d- -f1)
    make
    make install

    # enable netmap at boot, and make sure the netmap e1000 driver is used
    echo 'netmap' >> /etc/modules-load.d/modules.conf
    echo 'e1000-netmap' >> /etc/modules-load.d/modules.conf
    echo 'blacklist e1000' >> /etc/modprobe.d/blacklist-netmap.conf
    # echo 'blacklist virtio' >> /etc/modprobe.d/blacklist-netmap.conf

    # create rc.local
    echo '#!/bin/bash' >> /etc/rc.local
    chmod +x /etc/rc.local

    # configure netmap
    echo 'echo 1 > /sys/module/netmap/parameters/admode' >> /etc/rc.local
    echo 'echo 1000000 > /sys/module/netmap/parameters/buf_num' >> /etc/rc.local

    # various changes to /etc to let normal users use netmap
    echo 'KERNEL=="netmap", MODE="0666"' > /etc/udev/rules.d/netmap.rules
    echo '*   soft  memlock   unlimited' >> /etc/security/limits.conf
    echo '*   hard  memlock   unlimited' >> /etc/security/limits.conf
    echo '*   soft  core      unlimited' >> /etc/security/limits.conf
    echo '*   hard  core      unlimited' >> /etc/security/limits.conf

    # enable IOMMU and hugepages
    sed -i'' -e 's/GRUB_CMDLINE_LINUX=.*/GRUB_CMDLINE_LINUX="iommu=pt intel_iommu=on hugepages=512"/' /etc/default/grub
    update-grub
    echo 512 > /proc/sys/vm/nr_hugepages
    echo 112640 > /proc/sys/vm/min_free_kbytes
    echo 2634022912 > /proc/sys/kernel/shmmax

    echo 'vfio-pci' >> /etc/modules-load.d/modules.conf
    echo 'igb_uio' >> /etc/modules-load.d/modules.conf

    # build a new initrd
    depmod -a
    update-initramfs -u

    echo Please restart via \"vagrant up\"
    shutdown -P now
  SHELL
end
