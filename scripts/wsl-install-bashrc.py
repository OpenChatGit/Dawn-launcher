from pathlib import Path

path = Path("/home/nicol/.bashrc")
text = path.read_text(encoding="utf-8")
marker = "# dawn-wsl-env"
hook = """# dawn-wsl-env
if [ -z "${HOME:-}" ] || echo "$HOME" | grep -q ':' || echo "$HOME" | grep -q '^C'; then
    export HOME="/home/$(id -un)"
fi
export DISPLAY="${DISPLAY:-:0}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
if [ -d /mnt/wslg/runtime-dir ]; then
    export XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
fi
if [ -S /mnt/wslg/PulseServer ]; then
    export PULSE_SERVER="${PULSE_SERVER:-unix:/mnt/wslg/PulseServer}"
fi

"""
if marker not in text:
    path.write_text(hook + text, encoding="utf-8")
    print("bashrc: inserted dawn WSL env")
else:
    print("bashrc: already has dawn WSL env")
