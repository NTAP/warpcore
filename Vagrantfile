Vagrant.configure("2") do |config|
  (1..2).each do |i|
    config.vm.define "node#{i}" do |config|
      config.vm.hostname = "node#{i}"
      config.vm.box = "../netmap-box/package.box"
      config.vm.network "private_network", ip: "172.28.128.#{i+10}"
      config.vm.provision "shell", inline: <<-SHELL
        ifconfig enp0s8 add "dead:beef::#{i+10}/64"
      SHELL
    end
  end
end
