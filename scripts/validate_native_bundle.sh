#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PLUGIN=${1:-}
EXPECTED_VERSION=${2:-}

if [ -z "$PLUGIN" ] || [ ! -d "$PLUGIN" ]; then
  echo "Uso: $0 '/caminho/Rosto Natural.plugin' [versão]" >&2
  exit 2
fi

ACTUAL=$(/usr/bin/mktemp /private/tmp/rosto-native-actual.XXXXXX)
EXPECTED=$(/usr/bin/mktemp /private/tmp/rosto-native-expected.XXXXXX)
DEPENDENCIES=$(/usr/bin/mktemp /private/tmp/rosto-native-deps.XXXXXX)
PIPL_DUMP=$(/usr/bin/mktemp /private/tmp/rosto-native-pipl.XXXXXX)
PIPL_STRINGS=$(/usr/bin/mktemp /private/tmp/rosto-native-pipl-strings.XXXXXX)
DEFINED_SYMBOLS=$(/usr/bin/mktemp /private/tmp/rosto-native-symbols.XXXXXX)
cleanup() {
  /bin/rm -f "$ACTUAL" "$EXPECTED" "$DEPENDENCIES" \
    "$PIPL_DUMP" "$PIPL_STRINGS" "$DEFINED_SYMBOLS"
}
trap cleanup EXIT HUP INT TERM

if /usr/bin/find "$PLUGIN" -type l -print | /usr/bin/grep -q .; then
  echo "Bundle recusado: links simbólicos não são permitidos." >&2
  exit 2
fi

{
  printf '%s\n' \
    "Contents/Info.plist" \
    "Contents/MacOS/Rosto Natural" \
    "Contents/Resources/RostoNaturalPiPL.rsrc" \
    "Contents/Resources/face_landmarker.task" \
    "Contents/Resources/hair_segmenter.tflite" \
    "Contents/Frameworks/libRostoMediaPipe.dylib" \
    "Contents/Frameworks/libopencv_calib3d.3.4.dylib" \
    "Contents/Frameworks/libopencv_core.3.4.dylib" \
    "Contents/Frameworks/libopencv_features2d.3.4.dylib" \
    "Contents/Frameworks/libopencv_highgui.3.4.dylib" \
    "Contents/Frameworks/libopencv_imgcodecs.3.4.dylib" \
    "Contents/Frameworks/libopencv_imgproc.3.4.dylib" \
    "Contents/Frameworks/libopencv_video.3.4.dylib" \
    "Contents/Frameworks/libopencv_videoio.3.4.dylib"
  if [ -f "$PLUGIN/Contents/_CodeSignature/CodeResources" ]; then
    printf '%s\n' "Contents/_CodeSignature/CodeResources"
  fi
} | LC_ALL=C /usr/bin/sort > "$EXPECTED"

/usr/bin/find "$PLUGIN" -type f -print |
  /usr/bin/sed "s#^$PLUGIN/##" |
  LC_ALL=C /usr/bin/sort > "$ACTUAL"
if ! /usr/bin/cmp -s "$EXPECTED" "$ACTUAL"; then
  echo "Bundle recusado: inventário inesperado." >&2
  /usr/bin/diff -u "$EXPECTED" "$ACTUAL" >&2 || true
  exit 2
fi
while IFS= read -r relative; do
  if [ ! -s "$PLUGIN/$relative" ]; then
    echo "Bundle recusado: arquivo vazio: $relative" >&2
    exit 2
  fi
done < "$EXPECTED"

PLIST="$PLUGIN/Contents/Info.plist"
/usr/bin/plutil -lint "$PLIST" >/dev/null
BUNDLE_ID=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$PLIST")
BUNDLE_VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$PLIST")
EXECUTABLE_NAME=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$PLIST")
if [ "$BUNDLE_ID" != "com.lucas.RostoNatural" ] ||
   [ "$EXECUTABLE_NAME" != "Rosto Natural" ]; then
  echo "Bundle recusado: identidade ou executável inválido." >&2
  exit 2
fi
if [ -n "$EXPECTED_VERSION" ] && [ "$BUNDLE_VERSION" != "$EXPECTED_VERSION" ]; then
  echo "Bundle recusado: versão $BUNDLE_VERSION; esperada $EXPECTED_VERSION." >&2
  exit 2
fi

/usr/bin/cmp -s "$PROJECT_DIR/models/face_landmarker.task" \
  "$PLUGIN/Contents/Resources/face_landmarker.task" || {
    echo "Bundle recusado: modelo facial divergente." >&2
    exit 2
  }
/usr/bin/cmp -s "$PROJECT_DIR/models/hair_segmenter.tflite" \
  "$PLUGIN/Contents/Resources/hair_segmenter.tflite" || {
    echo "Bundle recusado: modelo de cabelo divergente." >&2
    exit 2
  }

/usr/bin/DeRez -useDF -only PiPL \
  "$PLUGIN/Contents/Resources/RostoNaturalPiPL.rsrc" > "$PIPL_DUMP"
if [ ! -s "$PIPL_DUMP" ]; then
  echo "Bundle recusado: recurso PiPL ausente ou ilegível." >&2
  exit 2
fi
/usr/bin/strings -a \
  "$PLUGIN/Contents/Resources/RostoNaturalPiPL.rsrc" > "$PIPL_STRINGS"
if ! /usr/bin/grep -Fxq 'EffectMain' "$PIPL_STRINGS" ||
   ! /usr/bin/grep -Fxq 'com.lucas.RostoNatural' "$PIPL_STRINGS" ||
   ! /usr/bin/grep -Fq 'https://github.com/lucasmef/rosto-natural' \
      "$PIPL_STRINGS"; then
  echo "Bundle recusado: PiPL não referencia a função/identidade esperada." >&2
  exit 2
fi

EXECUTABLE="$PLUGIN/Contents/MacOS/Rosto Natural"
FRAMEWORKS="$PLUGIN/Contents/Frameworks"
/usr/bin/nm -gU "$EXECUTABLE" |
  /usr/bin/awk '{print $NF}' | LC_ALL=C /usr/bin/sort -u > "$DEFINED_SYMBOLS"
if ! /usr/bin/grep -Fxq '_EffectMain' "$DEFINED_SYMBOLS" ||
   ! /usr/bin/grep -Fxq '_PluginDataEntryFunction2' "$DEFINED_SYMBOLS"; then
  echo "Bundle recusado: entradas Adobe obrigatórias ausentes." >&2
  exit 2
fi
while IFS= read -r symbol; do
  case "$symbol" in
    _EffectMain|_PluginDataEntryFunction2|__mh_bundle_header) ;;
    *)
      echo "Bundle recusado: símbolo global inesperado: $symbol" >&2
      exit 2
      ;;
  esac
done < "$DEFINED_SYMBOLS"
for macho in "$EXECUTABLE" "$FRAMEWORKS"/*.dylib; do
  if [ "$(/usr/bin/lipo -archs "$macho")" != "arm64" ]; then
    echo "Bundle recusado: arquitetura diferente de arm64: $macho" >&2
    exit 2
  fi
  /usr/bin/otool -L "$macho" |
    /usr/bin/sed -n '2,$s/^[[:space:]]*\([^[:space:]]*\).*/\1/p' \
      > "$DEPENDENCIES"
  while IFS= read -r dependency; do
    case "$dependency" in
      /System/Library/*|/usr/lib/*)
        ;;
      @rpath/*)
        name=${dependency##*/}
        if [ "$dependency" != "@rpath/$name" ]; then
          echo "Bundle recusado: caminho @rpath não canônico: $dependency" >&2
          exit 2
        fi
        if [ ! -s "$FRAMEWORKS/$name" ]; then
          echo "Bundle recusado: dependência local ausente: $dependency" >&2
          exit 2
        fi
        ;;
      @loader_path/*)
        name=${dependency##*/}
        if [ "$dependency" != "@loader_path/$name" ]; then
          echo "Bundle recusado: caminho @loader_path não canônico: $dependency" >&2
          exit 2
        fi
        if [ ! -s "$FRAMEWORKS/$name" ]; then
          echo "Bundle recusado: dependência local ausente: $dependency" >&2
          exit 2
        fi
        ;;
      *)
        echo "Bundle recusado: dependência absoluta/inesperada: $dependency" >&2
        exit 2
        ;;
    esac
  done < "$DEPENDENCIES"
  MINIMUM_OS=$(/usr/bin/otool -l "$macho" |
    /usr/bin/awk '/cmd LC_BUILD_VERSION/{seen=1} seen && /minos/{print $2; exit}')
  if [ -z "$MINIMUM_OS" ] ||
     ! /usr/bin/awk -v version="$MINIMUM_OS" \
       'BEGIN { exit !(version + 0 <= 14.0) }'; then
    echo "Bundle recusado: requisito macOS acima de 14.0: $macho" >&2
    exit 2
  fi
done

EXEC_RPATHS=$(/usr/bin/otool -l "$EXECUTABLE" |
  /usr/bin/awk '/cmd LC_RPATH/{getline; getline; print $2}')
if [ "$EXEC_RPATHS" != "@loader_path/../Frameworks" ]; then
  echo "Bundle recusado: LC_RPATH do efeito não é hermético." >&2
  printf '%s\n' "$EXEC_RPATHS" >&2
  exit 2
fi
for dylib in "$FRAMEWORKS"/*.dylib; do
  RPATHS=$(/usr/bin/otool -l "$dylib" |
    /usr/bin/awk '/cmd LC_RPATH/{getline; getline; print $2}')
  if [ "$RPATHS" != "@loader_path" ]; then
    echo "Bundle recusado: LC_RPATH não hermético: $dylib" >&2
    printf '%s\n' "$RPATHS" >&2
    exit 2
  fi
done

/usr/bin/codesign --verify --deep --strict "$PLUGIN"
printf 'Bundle nativo validado: %s (%s)\n' "$PLUGIN" "$BUNDLE_VERSION"
