This repo contains a [configuration file](.github/in-solidarity.yml) for
[in-solidarity-bot](https://github.com/jpoehnelt/in-solidarity-bot) that flags
the terms in [the IETF's list of problematic
terminology](https://github.com/ietf/terminology).

If you want to use this with your repository, you can do this at the top of your
repo:

``` shell
mkdir .github
git submodule add https://github.com/NTAP/isb-ietf-config.git .github/isb-ietf-config
ln -s isb-ietf-config/.github/in-solidarity.yml .
```

