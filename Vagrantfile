# This is a config file for vagrant (https://www.vagrantup.com/),
# which will install a VM inside which we can build and run warpcore.

Vagrant.configure("2") do |config|
  (1..3).each do |i|
    config.vm.define "node#{i}" do |node|
      node.vm.hostname = "node#{i}"

      # OS to use for the VM
      if i <= 2 then
        node.vm.box = "ubuntu/zesty64"
      else
        node.vm.box = "freebsd/FreeBSD-11.0-STABLE"
        node.vm.base_mac = "DECAFBAD00"
        node.ssh.shell = "/bin/sh"
        node.vm.synced_folder ".", "/vagrant", type: "rsync",
          rsync__exclude: ".git/"
      end

      # don't always check for box updates
      node.vm.box_check_update = false

      # since the first interface is a NAT and all VMs have the same
      # IP and possibly MAC address, create two private networks, one
      # for control and one for netmap experimentation
      node.vm.network "private_network", ip: "192.168.101.#{i}/24"
      node.vm.network "private_network", ip: "192.168.202.#{i}/24"

      # hardware configuration of the VM
      node.vm.provider "virtualbox" do |vb|
        # general settings
        vb.gui = false
        vb.memory = "2044"
        vb.cpus = 2
        vb.linked_clone = true
        vb.name = node.vm.hostname

        # use virtio for uplink, in case there is an issue with netmap's e1000
        vb.customize ["modifyvm", :id, "--nictype1", "virtio"]

        # allow promiscuous mode on the private  networks
        vb.customize ["modifyvm", :id, "--nicpromisc2", "allow-all"]
        vb.customize ["modifyvm", :id, "--nicpromisc3", "allow-all"]

        # per-VM serial log
        vb.customize ["modifyvm", :id, "--uartmode1", "file",
          File.join(Dir.pwd, "%s-console.log" % node.vm.hostname)]

        # better clock synchronization (to within 100ms)
        vb.customize [ "guestproperty", "set", :id,
          "/VirtualBox/GuestAdd/VBoxService/--timesync-set-threshold", 100 ]
      end

      # use custom, static, password-less SSH keys to authenticate between VMs
      ssh_pub_key = File.readlines(File.dirname(__FILE__) + "/misc/id_rsa.pub").join
      ssh_sec_key = File.readlines(File.dirname(__FILE__) + "/misc/id_rsa").join

      if i <= 2 then # Linux
        # apply some fixes to the VM OS, update it, and install some tools
        node.vm.provision "shell", inline: <<-SHELL
          # install common SSH keys, allow password-less auth
          echo "#{ssh_pub_key}" >> /home/ubuntu/.ssh/authorized_keys
          echo "#{ssh_pub_key}" > /home/ubuntu/.ssh/id_rsa.pub
          echo "#{ssh_sec_key}" > /home/ubuntu/.ssh/id_rsa
          echo "StrictHostKeyChecking no" > /home/ubuntu/.ssh/config
          echo "UserKnownHostsFile=/dev/null" >> /home/ubuntu/.ssh/config
          chown -R ubuntu:ubuntu .ssh
          chmod -R go-rwx .ssh/id_rsa

          # update apt catalog
          export DEBIAN_FRONTEND=noninteractive
          apt-get update

          # do a dist-upgrade and clean up
          # apt-get -y dist-upgrade
          # apt-get -y autoremove
          # apt-get -y autoclean

          # install some needed tools (libclang-common-3.9-dev for iwyu)
          apt-get -y install cmake git clang clang-tidy ninja-build dpkg-dev \
            iwyu libclang-common-3.9-dev

          # and some that I often use
          apt-get -y install htop silversearcher-ag gdb fish dwarves

          # change shell to fish
          chsh -s /usr/bin/fish ubuntu

          # # get Linux kernel sources, for building netmap
          apt-get source linux-image-$(uname -r)

          # compile and install netmap
          git clone https://github.com/luigirizzo/netmap ||
            cd ~/netmap && git pull
          # now, build and install netmap
          cd /home/ubuntu/netmap
          ./configure --driver-suffix=-netmap --no-ext-drivers \
            --kernel-sources=/home/ubuntu/linux-$(uname -r | cut -d- -f1)
          make -j8
          make install

          # enable netmap at boot, and make sure the netmap e1000 driver is used
          echo 'netmap' >> /etc/modules-load.d/modules.conf
          echo 'e1000-netmap' >> /etc/modules-load.d/modules.conf
          echo 'blacklist e1000' >> /etc/modprobe.d/blacklist-netmap.conf
          # echo 'blacklist virtio' >> /etc/modprobe.d/blacklist-netmap.conf

          # various changes to /etc to let normal users use netmap
          echo 'KERNEL=="netmap", MODE="0666"' > /etc/udev/rules.d/netmap.rules
          echo '*   soft  memlock   unlimited' >> /etc/security/limits.conf
          echo '*   hard  memlock   unlimited' >> /etc/security/limits.conf
          echo '*   soft  core      unlimited' >> /etc/security/limits.conf
          echo '*   hard  core      unlimited' >> /etc/security/limits.conf

          # build a new initrd
          depmod -a
          update-initramfs -u

          # XXX is there a way to automate this (reboot doesn't mount /vagrant)
          echo 'IMPORTANT: You need to "vagrant halt #{node.vm.hostname}"; ' \
            'vagrant up #{node.vm.hostname}" for netmap support!'
        SHELL

      else # FreeBSD
        node.vm.provision "shell", inline: <<-SHELL
          pkg update
          pkg upgrade
          pkg install sudo cmake nano git include-what-you-use ninja
        SHELL
      end
    end
  end
end
