#!/usr/bin/env bash
# Builds a 64-bit ygoprodll.exe (x64 Release) against 64-bit libraries built with vcpkg (x64-windows-static)
# and the 64-bit ocgcore import library from build-tools/core-x64.
# Usage: bash build-tools/build-x64.sh   (run from edopro-src)
set -euo pipefail
cd "$(dirname "$0")/.."
export DXSDK_DIR="$PWD/build-tools/d3d9sdk/"
python patches/apply_irrlicht_patch.py
[ -n "${PICS_URL:-}" ] || PICS_URL='https://pics.projectignis.org:2096/pics/{}.jpg'
[ -n "${FIELDS_URL:-}" ] || FIELDS_URL='https://pics.projectignis.org:2096/field/{}.png'
[ -n "${COVERS_URL:-}" ] || COVERS_URL='https://pics.projectignis.org:2096/pics/cover/{}.jpg'
./build-tools/premake5.exe vs2022 --architecture=x64 --prebuilt-core=build-tools --bundled-font=NotoSansJP-Regular.otf \
  --no-core=true --sound=miniaudio,sfml --no-joystick=true \
  --pics=\"$PICS_URL\" --fields=\"$FIELDS_URL\" --covers=\"$COVERS_URL\" >/dev/null
V="$(cygpath -w "$PWD/build-tools/vcpkg-x64/installed/x64-windows-static")"
C="$(cygpath -w "$PWD/build-tools/core-x64")"
export CL="/std:c++17 /I\"$V\include\""
export LINK="/LIBPATH:\"$V\lib\" /LIBPATH:\"$C\""
export _LINK_="fmt.lib freetype.lib jpeg.lib libpng16.lib zs.lib bz2.lib brotlicommon.lib brotlidec.lib brotlienc.lib http_parser.lib pcre.lib spng_static.lib bcrypt.lib normaliz.lib avrt.lib sqlite3.lib event.lib event_core.lib event_extra.lib git2.lib libssh2.lib libcurl.lib libssl.lib libcrypto.lib FLAC.lib ogg.lib vorbis.lib vorbisenc.lib vorbisfile.lib OpenAL32.lib"
MSB="C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" build/ygo.sln -m -p:Configuration=Release -p:Platform=x64 -p:WindowsTargetPlatformVersion=10.0.26100.0 \
  -t:ygoprodll -verbosity:minimal | tee build-tools/build-x64.log
ls -la bin/x64/release/ygoprodll.exe
