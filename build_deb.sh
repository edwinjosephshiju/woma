#!/bin/bash
set -e

INSTALL_DIR="release-woma"
DEB_DIR="woma-deb"
VERSION="3.12.13"
ARCH="amd64"

echo "Creating DEB package structure..."
mkdir -p $DEB_DIR/DEBIAN
cp -r $INSTALL_DIR/* $DEB_DIR/

# Ensure the executable exists
if [ ! -f "$DEB_DIR/usr/local/bin/python3.12" ] && [ ! -f "$DEB_DIR/usr/local/bin/woma" ]; then
    echo "Warning: Python executable not found in standard paths."
fi

# Create woma symlink
if [ -f "$DEB_DIR/usr/local/bin/python3.12" ] && [ ! -f "$DEB_DIR/usr/local/bin/woma" ]; then
    echo "Creating woma symlink..."
    ln -s python3.12 "$DEB_DIR/usr/local/bin/woma"
fi

cat <<EOF > $DEB_DIR/DEBIAN/control
Package: womapython
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH
Maintainer: WomaPython Team
Description: WomaPython Compiler with AI Transpilation
 A polyglot compiler that uses the Qwen2.5-Coder model to transpile pseudocode into valid Python.
EOF

cat <<'EOF' > $DEB_DIR/DEBIAN/postinst
#!/bin/bash
set -e

MODEL_URL="https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q5_k_m.gguf"
MODEL_DIR="/usr/share/woma"
MODEL_PATH="$MODEL_DIR/model.gguf"

if [ ! -f "$MODEL_PATH" ]; then
    echo "=========================================================="
    echo "WomaPython: Downloading AI Model (1.28 GB) to $MODEL_PATH"
    echo "This may take a few minutes depending on your connection."
    echo "=========================================================="
    mkdir -p "$MODEL_DIR"
    wget -q --show-progress -O "$MODEL_PATH" "$MODEL_URL"
    echo "Download complete."
fi

exit 0
EOF

chmod 755 $DEB_DIR/DEBIAN/postinst

echo "Building Debian package..."
dpkg-deb --build $DEB_DIR woma-linux-x86_64.deb
echo "Done! woma-linux-x86_64.deb created."
