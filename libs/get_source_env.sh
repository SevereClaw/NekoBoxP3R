export COMMIT_LIBNEKO="1c47a3af71990a7b2192e03292b4d246c308ef0b"
# sing-box version: the single source of truth is go/cmd/nekobox_core/go.mod
export SING_BOX_VERSION="$(grep -E '^\s*github.com/sagernet/sing-box ' go/cmd/nekobox_core/go.mod | awk '{print $2}' | sed 's/^v//')"
