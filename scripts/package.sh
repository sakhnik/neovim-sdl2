#!/bin/bash -e

if [[ "$WORKSPACE_MSYS2" ]]; then
  cd "$WORKSPACE_MSYS2"
fi

mkdir -p dist/{bin,lib,share}
cp build/nvim-sdl2.exe dist/bin
touch dist/bin/nvim-sdl2.exe.gui

dlls=$(ntldd.exe -R build/nvim-sdl2.exe | grep -Po "[^ ]+?msys64[^ ]+" | sort -u | grep -Po '[^\\]+$')
for dll in $dlls; do
  cp /mingw64/bin/$dll dist/bin
done

cp -r chocolatey/* dist/
version=$(grep -Po '(?<=set\(VERSION )[^) ]+' CMakeLists.txt)
if git describe --tags 2>/dev/null | grep -F "$version" >/dev/null 2>&1; then
  echo "Release version is ${version}"
else
  sha1=$(git rev-list --abbrev-commit -1 HEAD)
  version="${version}-dev${GITHUB_RUN_NUMBER}-${sha1}"
  echo "Development version is ${version}"
fi

sed -i "s|VERSION|${version}|" dist/*.nuspec
