Vagrant.configure("2") do |config|
  (1..3).each do |i|
    config.vm.define "node#{i}" do |config|
      config.vm.hostname = "node#{i}"
      config.vm.network "private_network", ip: "172.28.128.#{i+10}"

      if (i == 3)
        config.vm.box = "freebsd/FreeBSD-11.3-STABLE"
        config.ssh.shell = "sh"
        config.vm.synced_folder ".", "/vagrant", :nfs => true, id: "vagrant-root"
      else
        config.vm.box = "../netmap-box/package.box"
        config.vm.provision "shell", inline: <<-SHELL
          ifconfig enp0s8 add "dead:beef::#{i+10}/64"
        SHELL
      end
    end
  end
end
