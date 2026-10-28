#!/bin/bash
# Builds Gs71Menu.dll - the in-game overlay (ImGui + D3D11 Present hook).
set -e
export PATH=/mingw64/bin:/usr/bin:/bin

ROOT=/d/dsh_workspace/dsh_code/genshi/clientpatch
OUT=$ROOT/overlay/build
mkdir -p "$OUT"

IMGUI=$ROOT/vendor/imgui-docking
MH=$ROOT/vendor/minhook-master

CXXFLAGS="-std=c++17 -O2 -municode -fno-exceptions -fno-rtti -DNDEBUG \
 -I$IMGUI -I$IMGUI/backends -I$MH/include -I$ROOT/overlay -Wno-unused-parameter"

echo "== compiling =="
g++ $CXXFLAGS -c "$ROOT/overlay/entry.cpp"   -o "$OUT/entry.o"
g++ $CXXFLAGS -c "$ROOT/overlay/menu.cpp"    -o "$OUT/menu.o"
g++ $CXXFLAGS -c "$ROOT/overlay/bridge.cpp"  -o "$OUT/bridge.o"
g++ $CXXFLAGS -c "$IMGUI/imgui.cpp"          -o "$OUT/imgui.o"
g++ $CXXFLAGS -c "$IMGUI/imgui_draw.cpp"     -o "$OUT/imgui_draw.o"
g++ $CXXFLAGS -c "$IMGUI/imgui_tables.cpp"   -o "$OUT/imgui_tables.o"
g++ $CXXFLAGS -c "$IMGUI/imgui_widgets.cpp"  -o "$OUT/imgui_widgets.o"
g++ $CXXFLAGS -c "$IMGUI/backends/imgui_impl_dx11.cpp" -o "$OUT/imgui_impl_dx11.o"
g++ $CXXFLAGS -c "$IMGUI/backends/imgui_impl_win32.cpp" -o "$OUT/imgui_impl_win32.o"
gcc -O2 -c "$MH/src/hook.c"       -o "$OUT/mh_hook.o"       -I"$MH/include" -I"$MH/src"
gcc -O2 -c "$MH/src/buffer.c"     -o "$OUT/mh_buffer.o"     -I"$MH/include" -I"$MH/src"
gcc -O2 -c "$MH/src/trampoline.c" -o "$OUT/mh_trampoline.o" -I"$MH/include" -I"$MH/src" -I"$MH/src/hde"
for f in hde64 hde32 table64 table32; do
  if [ -f "$MH/src/hde/$f.c" ]; then
    gcc -O2 -c "$MH/src/hde/$f.c" -o "$OUT/mh_$f.o" -I"$MH/include" -I"$MH/src" -I"$MH/src/hde"
  fi
done

echo "== linking =="
g++ -shared -static -static-libgcc -static-libstdc++ -o "$OUT/Gs71Menu.dll" -Wl,-Map="$OUT/Gs71Menu.map" "$OUT"/*.o \
    -ld3d11 -ldxgi -ld3dcompiler -lwinhttp -lpsapi -luser32 -lgdi32 -ldwmapi -Wl,--kill-at

ls -la "$OUT/Gs71Menu.dll"
echo "OK"
