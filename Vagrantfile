# This is a config file for vagrant (https://www.vagrantup.com/),
# which will install a FreeBSD-11.0-STABLE VM, inside which we can build
# and run warpcore.

Vagrant.configure("2") do |config|
  # OS to use for the VM
  config.vm.box = "freebsd/FreeBSD-11.0-STABLE"
  # config.vm.box_version = "20160930.0.0"

  # don't always check for box updates
  config.vm.box_check_update = false

  # hardware configuration of the VM
  config.vm.provider "virtualbox" do |vb|
    vb.gui = true
    vb.memory = "1024"
    vb.cpus = 1
    vb.linked_clone = true
  end

  # there is no bash under FreeBSD by default
  config.ssh.shell = 'sh'

  # need to manually set a MAC address for FreeBSD?
  config.vm.base_mac = "0800DECAFBAD"

  # need to mount over a host-only network via NFS on FreeBSD
  config.vm.network "private_network", type: "dhcp"
  config.vm.synced_folder ".", "/vagrant", type: "nfs"

  # mount the main quickie directory as well
  # config.vm.synced_folder "..", "/quickie"

  # apply some fixes to the VM OS, update it, and install some tools
  config.vm.provision "shell", inline: <<-SHELL
    pkg install -y -q bash htop git gmake cmake
  SHELL
end
