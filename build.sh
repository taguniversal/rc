#!/bin/bash
# Because meson doesn't know about clean, apparently
echo "🧹 Cleaning build output..."
rm -rf build/out

echo "⚙️  Starting build..."
meson compile -C build
