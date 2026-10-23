#!/bin/sh
set -eu
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SKILL_HOME=${TIBO_SKILL_HOME:-"$HOME/.agents/skills"}
DEST="$SKILL_HOME/tibo-reset"
[ "$(uname -s)" = Darwin ] || { echo '此脚本用于 macOS；Windows 请使用 install-win.ps1。' >&2; exit 1; }
[ ! -e "$DEST" ] && [ ! -L "$DEST" ] || { echo "已存在 $DEST；请先移动旧版本备份，再重新安装。" >&2; exit 1; }
mkdir -p "$SKILL_HOME"
cp -R "$BASE/tibo-reset" "$DEST"
printf '已安装轻量技能：%s\n首次查询时按需下载程序。\n' "$DEST"
