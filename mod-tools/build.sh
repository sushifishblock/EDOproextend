#!/usr/bin/env bash
# Rebuilds ygoprodll.exe (Win32 Release) with the v141 toolset and prebuilt vcpkg libs.
# Usage: bash build-tools/build.sh   (run from edopro-src)
set -euo pipefail
cd "$(dirname "$0")/.."
export DXSDK_DIR="$PWD/build-tools/d3d9sdk/"
# picture servers used by the official client (found in the official EDOPro.exe); no update url on purpose
[ -n "${PICS_URL:-}" ] || PICS_URL='https://pics.projectignis.org:2096/pics/{}.jpg'
[ -n "${FIELDS_URL:-}" ] || FIELDS_URL='https://pics.projectignis.org:2096/field/{}.png'
[ -n "${COVERS_URL:-}" ] || COVERS_URL='https://pics.projectignis.org:2096/pics/cover/{}.jpg'
./build-tools/premake5.exe vs2017 --prebuilt-core=build-tools --bundled-font=NotoSansJP-Regular.otf \
  --no-core=true --sound=miniaudio,sfml --no-joystick=true \
  --pics=\"$PICS_URL\" --fields=\"$FIELDS_URL\" --covers=\"$COVERS_URL\" >/dev/null
V="$(cygpath -w "$PWD/build-tools/vcpkg2/installed/x86-windows-static")"
S="$(cygpath -w "$PWD/build-tools/stublibs")"
export CL="/std:c++17 /I\"$V\include\""
export LINK="/LIBPATH:\"$V\lib\" /LIBPATH:\"$S\" /LARGEADDRESSAWARE"
export _LINK_="fmt.lib freetype.lib jpeg.lib libpng16.lib zlib.lib bz2.lib sqlite3.lib event.lib event_core.lib event_extra.lib git2.lib libssh2.lib libcurl.lib libssl.lib libcrypto.lib FLAC.lib ogg.lib vorbis.lib vorbisenc.lib vorbisfile.lib OpenAL32.lib"
MSB="C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe"
"$MSB" build/ygo.sln -m -p:Configuration=Release -p:Platform=Win32 -p:WindowsTargetPlatformVersion=10.0.19041.0 \
  -t:ygoprodll -verbosity:minimal | tee build-tools/build.log
ls -la bin/release/ygoprodll.exe
