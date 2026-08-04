# Adaptador Adobe

O adaptador expõe os parâmetros e converte quadros do SDK Adobe. O tratamento
visual fica em `rosto_core`; Face Landmarker e Hair Segmenter entram pela
biblioteca `libRostoMediaPipe.dylib`.

O build deliberadamente falha se a ponte MediaPipe não existir. Isso impede a
criação acidental de um efeito que apareça no Premiere mas não detecte o rosto.

## Build

1. Coloque o SDK atual em `vendor/AfterEffectsSDK` ou defina `AE_SDK_ROOT`.
2. Instale o Xcode completo e a Metal Toolchain.
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

O build validado usa Adobe SDK 25.6, Xcode 26.6 build 17F113 e Metal
Toolchain 17F109. `build_metal.sh` produz `.air` e `.metallib`, mas o CMake e o
bundle atuais ainda não os copiam nem carregam; o plugin instalado e o ZIP
nativo permanecem no caminho CPU clássico de 8 bits.

O adaptador anuncia somente o render clássico de 8 bits atualmente
implementado. Smart Render, deep color e float não são anunciados antes de
existirem os respectivos command paths.

## Compatibilidade e gate no Premiere 26.3

O bundle 0.11.0 foi instalado e validado. O Premiere expõe o match name
canônico `com.lucas.RostoNatural` como `AE.com.lucas.RostoNatural`; os nomes
`Lábios` e `Cor dos lábios` chegam com mojibake; e Color não pode ser lida com
`getValueAtTime`. O painel canonicaliza o prefixo e os aliases e usa
`ppro.Color` para a escrita.

Em projeto temporário seguro, efeito e painel carregaram, a aplicação e os
ajustes ao vivo passaram, e Undo/Redo foram confirmados por leitura do valor
nativo. O preset facial, cabelo 0/70/100, a análise de 210 quadros e duas
exportações completas passaram. O render combinado 720×1280 de 7 s terminou em
4,529 s no caminho CPU e decodificou 210/210 quadros.

## Análise e cache

O painel agenda `_Capture Token` com interpolação HOLD e associa cada resposta
ao tempo de origem retornado por `PF_InData.current_time/time_scale`. A captura
é um side channel: o render normal usa apenas o cache já confirmado, e uma
análise nova só promove `_Analysis ID` depois de escrever e validar o cache.

A publicação temporária exige `_Capture Mode`, `_Capture Session` e uma lease
com menos de 10 segundos. O painel renova a lease a cada 2 segundos, inclusive
durante uma exportação individual; unload/crash torna novas publicações inertes
em até 10 segundos. Sessões incompletas são coletadas depois de 30 segundos
somente se root, pasta, markers e inventário forem arquivos regulares dentro de
`/private/tmp/RostoNatural`; symlinks e conteúdo inesperado falham fechados.
Um encerramento abrupto pode deixar JPG/JSON temporário nessa pasta; a próxima
análise remove a sessão incompleta validada, ou o próprio macOS limpa `/tmp`.

No gate real, a análise detectou rosto em 210/210 quadros em 3,54 s. A pasta
promovida continha somente `landmarks.bin` e `metadata.json`; capture mode,
session e token voltaram a zero e sem keyframes. Falhas anteriores foram
recusadas antes da promoção e suas sessões incompletas foram removidas. O teste
não forçou crash/unload do painel. A proteção é baseada na expiração da lease,
e a coleta segura de sessões expiradas possui cobertura determinística.

Limites atuais da análise: sequência progressiva, pixels quadrados, até 7.200
quadros e velocidade constante. Trim, 0,5×, 1×, 2× e reverso são mapeados; uma
rampa de velocidade é recusada sem substituir o cache anterior. Esses
invariantes passam nos testes. Como a API UXP 26.3 só lê velocidade/reverso e o
controle visual do macOS não autorizou a alteração, a matriz real de retiming
não foi forçada com QE, API privada ou edição de `.prproj`.

## Publicação e instalação

`build_adobe.sh`, a ponte MediaPipe e os packagers compartilham um lock global,
staging, allowlists, hashes e rollback recuperável. Uma interrupção tratável é
mascarada durante os renames críticos; `SIGKILL` continua podendo deixar o
diretório final momentaneamente ausente, mas o conjunto anterior permanece no
backup único para recuperação manual.

O instalador valida source/stage/final, exige Premiere Pro, PProHeadless, Adobe
Media Encoder, After Effects e `aerender` fechados e usa `--confirm`; uma
substituição exige também `--replace` e preserva o bundle anterior.
`uninstall_when_ready.sh --confirm` faz as mesmas verificações e move a
instalação para um backup recuperável em vez de apagá-la.
