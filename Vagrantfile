Vagrant.configure("2") do |config|

  # OS to use for the VM
  config.vm.box = "ubuntu/zesty64" # artful64

  # don't always check for box updates
  config.vm.box_check_update = false

  config.vm.network "private_network", type: "dhcp"

  config.vm.provision "file", source: "/etc/letsencrypt/live/slate.eggert.org",
    destination: "~/slate.eggert.org"

  # hardware configuration of the VM
  config.vm.provider "virtualbox" do |vb|
    vb.gui = false
    vb.memory = "1024"
    vb.cpus = 2
    vb.linked_clone = true
    vb.name = config.vm.hostname

    # use virtio for uplink, in case there is an issue with netmap's e1000
    vb.customize ["modifyvm", :id, "--nictype1", "virtio"]

    # per-VM serial log
    # vb.customize ["modifyvm", :id, "--uartmode1", "file",
    #   File.join(Dir.pwd, "%s-console.log" % config.vm.hostname)]

    # better clock synchronization (to within 100ms)
    vb.customize [ "guestproperty", "set", :id,
      "/VirtualBox/GuestAdd/VBoxService/--timesync-set-threshold", 100 ]
  end

  # apply some fixes to the VM OS, update it, and install some tools
  config.vm.provision "shell", inline: <<-SHELL
    export DEBIAN_FRONTEND=noninteractive

    git config --global user.email lars@netapp.com
    git config --global user.name "Lars Eggert"

    # update the box
    apt-get -y update
    apt-get -y upgrade
    apt-get -y autoremove
    apt-get -y autoclean

    # install some tools that are needed
    apt-get -y install git tmux ninja-build libev-dev libssl-dev g++ fish \
      pkg-config htop silversearcher-ag linux-tools-common linux-tools-generic \
      gdb valgrind mercurial libhttp-parser-dev iwyu apache2 libbsd-dev \
      clang-tidy libclang-common-3.9-dev

    # install recent cmake
    wget -q https://cmake.org/files/v3.9/cmake-3.9.5-Linux-x86_64.sh
    sh cmake-3.9.5-Linux-x86_64.sh --skip-license --prefix=/usr/local

    # change shell to fish
    chsh -s /usr/bin/fish ubuntu

    # get Linux kernel sources, for building netmap
    apt-get source linux-image-$(uname -r)

    # compile and install netmap
    git clone https://github.com/luigirizzo/netmap
    cd /home/ubuntu/netmap
    ./configure --driver-suffix=-netmap --no-ext-drivers \
      --kernel-sources=/home/ubuntu/linux-$(uname -r | cut -d- -f1)
    make
    make install

    # enable netmap at boot, and make sure the netmap e1000 driver is used
    echo 'netmap' >> /etc/modules-load.d/modules.conf
    echo 'e1000-netmap' >> /etc/modules-load.d/modules.conf
    echo 'blacklist e1000' >> /etc/modprobe.d/blacklist-netmap.conf
    echo 'blacklist virtio' >> /etc/modprobe.d/blacklist-netmap.conf

    # configure netmap
    echo 'echo 1 > /sys/module/netmap/parameters/admode' >> /etc/rc.local
    echo 'echo 1000000 > /sys/module/netmap/parameters/buf_num' >> /etc/rc.local

    # various changes to /etc to let normal users use netmap
    echo 'KERNEL=="netmap", MODE="0666"' > /etc/udev/rules.d/netmap.rules
    echo '*   soft  memlock   unlimited' >> /etc/security/limits.conf
    echo '*   hard  memlock   unlimited' >> /etc/security/limits.conf
    echo '*   soft  core      unlimited' >> /etc/security/limits.conf
    echo '*   hard  core      unlimited' >> /etc/security/limits.conf

    # build a new initrd
    depmod -a
    update-initramfs -u

    echo Please restart via \"vagrant up\"
    shutdown -P now
  SHELL
end
