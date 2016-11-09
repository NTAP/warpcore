# This is a config file for vagrant (https://www.vagrantup.com/),
# which will install a VM inside which we can build and run warpcore.

Vagrant.configure("2") do |config|
  (1..2).each do |i|
    config.vm.define "node#{i}" do |node|
      node.vm.hostname = "node#{i}"

      # OS to use for the VM
      node.vm.box = "ubuntu/xenial64"
      node.vm.box_version = "20160930.0.0"

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
        vb.memory = "1024"
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
      ssh_pub_key = File.readlines("scripts/id_rsa.pub").join
      ssh_sec_key = File.readlines("scripts/id_rsa").join

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

        # install some tools that are needed
        apt-get -y install cmake cmake-curses-gui git dpkg-dev xinetd

        # and some that I often use
        apt-get -y install htop silversearcher-ag linux-tools-common \
          linux-tools-generic gdb nmap fish

        # enable xinetd and remove rate limits
        find /etc/xinetd.d -type f -and -exec \
          sed -i -e 's/disable.*/disable\t\t= no/g' {} \;
        sed -i -e 's/{/{\ninstances = UNLIMITED\ncps = 0 0/' /etc/xinetd.conf

        # change shell to fish
        chsh -s /usr/bin/fish ubuntu

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
        echo 'IMPORTANT: You need to "vagrant reload #{node.vm.hostname}"' \
          'for netmap support!'
      SHELL

    end
  end
end
