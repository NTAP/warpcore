# This is a config file for vagrant (https://www.vagrantup.com/),
# which will install a VM inside which we can build and run warpcore.

Vagrant.configure("2") do |config|
  (1..2).each do |i|
    config.vm.define "node-#{i}" do |node|
      node.vm.hostname = "node-#{i}"

      # OS to use for the VM
      node.vm.box = "ubuntu/xenial64"
      node.vm.box_version = "20160930.0.0"

      # don't always check for box updates
      node.vm.box_check_update = false

      node.vm.network "private_network", ip: "10.11.11.#{i}/24"
      # XXX v6 doesn't seem to work?
      # node.vm.network "private_network", ip: "fddd:deca:fbad::#{i}/64"

      # hardware configuration of the VM
      node.vm.provider "virtualbox" do |vb|
        vb.gui = false
        vb.memory = "1024"
        vb.cpus = 1
        vb.linked_clone = true
      end

      # SSH config
      node.ssh.forward_agent = true
      # XXX this configurable causes an error?
      # node.ssh.forward_x11 - true

      # apply some fixes to the VM OS, update it, and install some tools
      node.vm.provision "shell", inline: <<-SHELL
        export DEBIAN_FRONTEND=noninteractive

        # update apt catalog
        apt-get update

        # install some tools that are needed
        apt-get -y install cmake git dpkg-dev

        # and some that I often use
        apt-get -y install htop silversearcher-ag

        # get Linux kernel sources, for building netmap
        apt-get source linux-image-$(uname -r)

        # do a dist-upgrade and clean up
        # XXX might update kernel, which makes building netmap difficult
        # apt-get -y dist-upgrade
        # apt-get -y autoremove
        # apt-get -y autoclean

        # compile and install netmap
        git clone https://github.com/luigirizzo/netmap ||
          cd ~/netmap && git pull
        # now, build and install netmap
        cd /home/ubuntu/netmap
        ./configure --driver-suffix=-netmap \
          --kernel-sources=/home/ubuntu/linux-$(uname -r | cut -d- -f1)
        make
        make install

        # enable netmap at boot, and make sure the netmap e1000 driver is used
        echo 'netmap' > /etc/modules-load.d/netmap.conf
        echo 'e1000-netmap' >> /etc/modules-load.d/netmap.conf
        echo 'blacklist e1000' > /etc/modprobe.d/blacklist-netmap.conf
        echo 'blacklist virtio' >> /etc/modprobe.d/blacklist-netmap.conf

        # various changes to /etc to let normal users use netmap
        echo 'KERNEL=="netmap", MODE="0666"' > /etc/udev/rules.d/netmap.rules
        echo '*   soft  memlock   unlimited' >> /etc/security/limits.conf
        echo '*   hard  memlock   unlimited' >> /etc/security/limits.conf

        # build a new initrd
        depmod -a
        update-initramfs -u

        # XXX is there a way to automate this (reboot doesn't mount /vagrant)
        echo 'You need to "vagrant reload" to reboot with netmap support'
      SHELL

    end
  end
end
