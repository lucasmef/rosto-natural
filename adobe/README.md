# Adaptador Adobe

O adaptador expõe os parâmetros e converte quadros do SDK Adobe. O tratamento
visual fica em `rosto_core`; Face Landmarker e Hair Segmenter entram pela
biblioteca `libRostoMediaPipe.dylib`.

O build deliberadamente falha se a ponte MediaPipe não existir. Isso impede a
criação acidental de um efeito que apareça no Premiere mas não detecte o rosto.

## Build

1. Coloque o SDK atual em `vendor/AfterEffectsSDK` ou defina `AE_SDK_ROOT`.
2. Instale o Xcode completo.
3. Execute:

```sh
ROSTO_BUILD_JOBS=4 ./scripts/prepare_mediapipe.sh
./scripts/build_adobe.sh
```

O CMake:

- compila para arm64;
- liga `rosto_core` e a ponte MediaPipe;
- copia modelos para `Contents/Resources`;
- copia a ponte para `Contents/Frameworks`;
- define o rpath relativo do bundle;
- compila `RostoNaturalPiPL.r` com Rez;
- verifica que o recurso PiPL foi criado.

O script aplica assinatura ad hoc para testes locais e valida o bundle. Uma
identidade Apple de distribuição só é necessária para entregar o plugin a
outros Macs.

O adaptador anuncia somente o render clássico de 8 bits atualmente
implementado. Smart Render, deep color e float não são anunciados antes de
existirem os respectivos command paths.
