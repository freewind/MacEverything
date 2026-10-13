#!/usr/bin/env bash
# 编译 Intel(x86_64) 版 MacEverything，安装到 /Applications，并打包 DMG。
#
# 用法：./build-mac.sh
# 依赖：Xcode、Homebrew re2（brew install re2）

set -euo pipefail

cd "$(dirname "$0")"

PROJECT="MacEverything.xcodeproj"
SCHEME="MacEverything"
CONFIGURATION="Release"
BUILD_DIR="build"
APP_NAME="MacEverything.app"
DMG_NAME="MacEverything-intel.dmg"
INSTALL_DIR="/Applications"

echo "==> 检查构建依赖"

if ! command -v xcodebuild >/dev/null 2>&1; then
  echo "错误：未找到 xcodebuild，请先安装 Xcode。" >&2
  exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "错误：未找到 Homebrew，请先安装。" >&2
  exit 1
fi

HOMEBREW_PREFIX="$(brew --prefix)"
if [ ! -d "$HOMEBREW_PREFIX/opt/re2" ]; then
  echo "错误：未找到 re2，请先运行：brew install re2" >&2
  exit 1
fi

echo "Homebrew 前缀：$HOMEBREW_PREFIX"
echo "re2 路径：$(brew --prefix re2)"

echo "==> 构建 $SCHEME（$CONFIGURATION，x86_64）"

xcodebuild \
  -project "$PROJECT" \
  -scheme "$SCHEME" \
  -configuration "$CONFIGURATION" \
  ARCHS=x86_64 \
  ONLY_ACTIVE_ARCH=NO \
  CODE_SIGN_STYLE=Manual \
  CODE_SIGN_IDENTITY="-" \
  SYMROOT="$BUILD_DIR" \
  build

APP_PATH="$BUILD_DIR/$CONFIGURATION/$APP_NAME"
MCP_PATH="$APP_PATH/Contents/MacOS/MacEverythingMCP"

if [ ! -d "$APP_PATH" ]; then
  echo "错误：构建产物不存在：$APP_PATH" >&2
  exit 1
fi

if [ ! -x "$MCP_PATH" ]; then
  echo "错误：MCP 二进制不存在：$MCP_PATH" >&2
  exit 1
fi

echo "==> 安装到 $INSTALL_DIR"

if pgrep -x MacEverything >/dev/null 2>&1; then
  echo "退出正在运行的 MacEverything"
  osascript -e 'quit app "MacEverything"' 2>/dev/null || pkill -x MacEverything || true
  sleep 2
fi

rm -rf "$INSTALL_DIR/$APP_NAME"
ditto "$APP_PATH" "$INSTALL_DIR/$APP_NAME"

echo "==> 打包 DMG"

rm -f "$DMG_NAME"
hdiutil create -volname MacEverything \
  -srcfolder "$APP_PATH" \
  -ov -format UDZO "$DMG_NAME" >/dev/null

echo "==> 完成"
echo "app:  $INSTALL_DIR/$APP_NAME"
echo "mcp:  $INSTALL_DIR/$APP_NAME/Contents/MacOS/MacEverythingMCP"
echo "dmg:  $(pwd)/$DMG_NAME"
echo "架构: $(lipo -archs "$INSTALL_DIR/$APP_NAME/Contents/MacOS/MacEverything")"
