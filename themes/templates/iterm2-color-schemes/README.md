# Generating nchat themes from iTerm2 Color Schemes

Steps to generate nchat themes:

    git clone https://github.com/d99kris/nchat
    git clone https://github.com/mbadolato/iTerm2-Color-Schemes
    cp nchat/themes/templates/iterm2-color-schemes/nchat-*.conf iTerm2-Color-Schemes/tools/templates/
    cd iTerm2-Color-Schemes
    mkdir -p nchat-color nchat-usercolor
    cd tools
    pip install -U rich
    python3 ./gen.py -t nchat-color
    python3 ./gen.py -t nchat-usercolor
    cd ..

List generated themes:

    ls nchat-color

Steps to use one theme, in this example `zenwritten_dark`:

    cp nchat-color/zenwritten_dark.conf ~/.nchat/color.conf
    cp nchat-usercolor/zenwritten_dark.conf ~/.nchat/usercolor.conf

