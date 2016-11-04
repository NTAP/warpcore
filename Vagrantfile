# This is a config file for vagrant (https://www.vagrantup.com/),
# which will install a VM inside which we can build and run warpcore.

Vagrant.configure("2") do |config|
  # OS to use for the VM
  config.vm.box = "ubuntu/xenial64"
  config.vm.box_version = "20160930.0.0"

  # don't always check for box updates
  config.vm.box_check_update = false

  config.vm.network "private_network", type: "dhcp", nic_type: "Am79C970A"
  config.vm.network "private_network", type: "dhcp", nic_type: "Am79C973"
  config.vm.network "private_network", type: "dhcp", nic_type: "virtio"

  # hardware configuration of the VM
  config.vm.provider "virtualbox" do |vb|
    vb.gui = false
    vb.memory = "1024"
    vb.cpus = 1
    vb.linked_clone = true
  end

  # apply some fixes to the VM OS, update it, and install some tools
  config.vm.provision "shell", inline: <<-SHELL
    export DEBIAN_FRONTEND=noninteractive

    # update the box
    apt-get update
    apt-get -y dist-upgrade

    # install some tools that are needed, or that I often use
    apt-get -y install htop silversearcher-ag daemon cmake git dpkg-dev

    # get Linux kernel sources, for netmap
    apt-get source linux-image-$(uname -r)

    # clean up
    apt-get -y autoremove
    apt-get -y autoclean

    # compile and install netmap
    git clone https://github.com/luigirizzo/netmap.git ||
      cd ~/netmap && git pull
  SHELL

  # reboot the box, in case we installed a new kernel
  config.vm.provision :reload

  config.vm.provision "shell", inline: <<-SHELL
    # now, build and install netmap
    cd /home/ubuntu/netmap
    ./configure --kernel-sources=/home/ubuntu/linux-$(uname -r | cut -d- -f1)
    make
    make install
    insmod netmap.ko
    echo "netmap" > /etc/modules-load.d/netmap.conf
  SHELL


end
