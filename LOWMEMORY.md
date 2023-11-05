Building on Low Memory / RAM Systems
====================================
The Telegram client library subcomponent requires relatively large amount of
RAM to build by default (3.5GB using g++, and 1.5 GB for clang++). It is
possible to adjust the Telegram client library source code so that it requires
less RAM (but takes longer time). Doing so reduces the memory requirement to
around 1GB under g++ and 0.5GB for clang++.

Steps to build nchat on a low memory system:

**Extra Dependencies (Linux)**

    sudo apt install php-cli clang

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Setup**

    mkdir -p build && cd build
    CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake ..

**Split source (optional)**

    cmake --build . --target prepare_cross_compiling
    cd ../lib/tgchat/ext/td ; php SplitSource.php ; cd -

**Build**

    make -s

**Install**

    sudo make install

**Revert Source Code Split (Optional)**

    cd ../lib/tgchat/ext/td ; php SplitSource.php --undo ; cd -

Arch Linux
----------
**Source**

    git clone https://aur.archlinux.org/nchat-git.git && cd nchat-git

**Prepare**

    Open PKGBUILD in your favourite editor.
    Add `php` and `clang` on depends array.
    Change the `_install_mode` to `slow`.

**Build**

    makepkg -s

**Install**

    makepkg -i

Fedora
------
**Extra Dependencies**

    sudo dnf install php-cli

**Source**

    git clone https://github.com/d99kris/nchat && cd nchat

**Build**

    mkdir -p build && cd build
    CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake ..
    cmake --build . --target prepare_cross_compiling
    cd ../lib/tgchat/ext/td ; php SplitSource.php ; cd -
    make -s

**Install**

    sudo make install

