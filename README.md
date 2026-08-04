# Rosto Natural

Efeito local de aprimoramento facial, maquiagem leve e acabamento de cabelos
rebeldes para Adobe Premiere Pro. A v0.11 foi calibrada usando os 17 pares
`cru / -facetune` encontrados neste Mac. O FaceTune é a única referência
aprovada e o efeito não altera a geometria do rosto.

## Recursos

- suavização multifrequência com preservação adaptativa de textura;
- máscara exclusivamente interna e proteção RGB no contorno;
- controles de pele, textura, olheiras, tom, brilho, olhos, blush e lábios;
- Face Mesh de 478 pontos;
- estabilização temporal com mediana de 5 quadros, liberação por movimento e
  interpolação de falhas curtas;
- remoção de cabelos rebeldes com HairSegmenter, proteção da massa principal e
  reconstrução esparsa do fundo;
- slider de cabelos de 0 a 100: zero é bypass, 70 é natural e 100 alcança fios
  mais claros e longos;
- painel UXP 0.11 e adaptador de efeito com match name canônico
  `com.lucas.RostoNatural`; o Premiere 26.3 o expõe na cadeia como
  `AE.com.lucas.RostoNatural`;
- núcleo CPU de referência e shader Metal com a mesma matemática facial.

## Preset aprovado

| Controle | Valor |
|---|---:|
| Intensidade | 72 |
| Pele | 58 |
| Preservar textura | 75 |
| Uniformizar tom | 24 |
| Olheiras | 42 |
| Brilho | 30 |
| Olhos | 18 |
| Cabelos rebeldes | 0 |
| Blush | 0 |
| Lábios | 20 |

## Portão FaceTune

São avaliados três quadros de cada um dos 17 pares reais, totalizando 51
amostras. A comparação remove somente a deriva global da recodificação,
estimada fora do rosto. Qualquer tratamento local continua visível na métrica.

| Medida | Rosto Natural | FaceTune |
|---|---:|---:|
| Microtextura | 0,715541 | 0,706220 |
| Textura fina | 0,640518 | 0,634980 |
| Textura média | 0,652850 | 0,662981 |
| Nitidez da mandíbula | 0,989655 | 0,948513 |
| Alteração fora do rosto (0–255) | 0,015519 | 2,014436 |
| Luminância da pele | -0,002251 | -0,001298 |
| Luminância das olheiras | -0,016058 | -0,015642 |
| Saturação dos olhos | -0,021593 | -0,020790 |
| Luminância dos olhos | -0,003425 | -0,003101 |
| Saturação dos lábios | +0,012897 | +0,012508 |
| Luminância dos lábios | -0,003626 | -0,003195 |

O tratamento ficou 7,98% mais próximo do FaceTune em cor e 15,15% em
estrutura do que o vídeo cru. O pior dos 17 vídeos ainda melhorou 2,82% em cor
e 9,18% em estrutura. O portão tem 17 critérios e falha se qualquer tolerância
for ultrapassada.

Relatórios:

- `facetune_validation_gate.json`
- `facetune_semantic_gate.json`
- `facetune_contour_gate.json`
- `facetune_texture_gate.json`
- `facetune_similarity_gate.json`

Execute novamente somente sobre uma pasta de referência explicitamente
autorizada:

```sh
./scripts/validate_against_facetune.sh "/caminho/explicitamente-autorizado"
```

Em 03/08/2026 o resultado 17/17 foi reagregado a partir dos quatro relatórios
existentes, cobrindo 51 amostras. Os vídeos pessoais de origem não foram
relidos nessa execução.

## Cabelos rebeldes

A inferência do HairSegmenter roda em resolução de análise, separa a massa
principal do penteado e promove duas máscaras suavizadas para a resolução
original: área permitida e cabelo protegido. A reconstrução final ocorre no
núcleo C++ usado pelo adaptador Adobe.

Na foto de validação real, em intensidade 100:

| Medida | Resultado |
|---|---:|
| Área alterada | 2,297989% |
| Pixels alterados no rosto | 0 |
| Pixels alterados no cabelo protegido | 0 |
| Pixels alterados fora da área permitida | 0 |
| Energia de linha antes | 5,725599 |
| Energia de linha depois | 3,842655 |

Em intensidade 70, a máscara permitida ocupa 1,98% da imagem de análise; em
100, 3,41%. A retenção de cabelo forte foi 100% e a sobreposição entre as
máscaras permitida/protegida foi zero.

Relatórios e prévia:

- `native_flyaway_analysis_gate.json`
- `native_flyaway_render_gate.json`
- `artifacts/native-flyaway-debug-v20-100.png`

## Testes rápidos

```sh
./scripts/build_core.sh
./scripts/build_cmake.sh
python3 tools/validate_adobe_contract.py
```

Resultado atual:

- 16 testes do núcleo C++;
- 1 teste do adaptador C++/MediaPipe;
- 12 testes do analisador UXP e 9 testes da ponte UXP/Premiere;
- 18 verificações do contrato Adobe;
- referência CPU offline 3840×2160: aproximadamente 0,57 s por quadro.

O adaptador Adobe implementa hoje somente o caminho CPU clássico de 8 bits.
Os artefatos `.air` e `.metallib` compilam com a toolchain oficial, mas ainda
não são carregados pelo render nem incorporados ao bundle/ZIP nativo. Portanto
não há alegação de Smart Render, float/deep color ou reprodução 4K fluida. O
número acima é benchmark offline, não desempenho medido no Premiere.

## MediaPipe nativo

`mediapipe_bridge/` define uma ABI C pequena e estável entre o efeito e o
MediaPipe. O build fixado usa:

- MediaPipe 0.10.35, commit da tag verificado;
- Bazel 7.4.1 arm64, checksum oficial verificado;
- Face Landmarker com 478 pontos;
- Hair Segmenter com máscara de probabilidade;
- CPU/XNNPACK, sem dependência de GPU para a inferência.

Preparação:

```sh
ROSTO_BUILD_JOBS=4 ./scripts/prepare_mediapipe.sh
```

O build usa no máximo quatro tarefas, grava detalhes em `build-mediapipe.log`
e publica a ponte mais oito dylibs OpenCV como um conjunto transacional. O
checkout está fixado no commit
`f8ef212d5c962c0e853db7e59d217056b187084b`; arquitetura, assinatura, rpaths,
dependências, versão mínima do macOS e hashes são revalidados antes e depois do
swap recuperável.

Teste de inferência real após a biblioteca existir:

```sh
PYTHONPATH=.python-legacy python3 tools/test_mediapipe_bridge.py \
  entrada.jpg
```

## Build Adobe sem abrir o Premiere

Pré-requisitos:

1. SDK C++ atual da Adobe em `vendor/AfterEffectsSDK`, ou `AE_SDK_ROOT`;
2. Xcode completo;
3. ponte MediaPipe compilada;
4. UXP Developer Tool para gerar o `.ccx`.

Diagnóstico:

```sh
./scripts/preflight.sh
```

Bundle nativo:

```sh
./scripts/build_adobe.sh
```

O build:

- exige MediaPipe, evitando gerar um efeito que carregue sem tratar;
- copia os dois modelos e a biblioteca dinâmica para o bundle;
- compila o PiPL com Rez;
- limita a arquitetura a arm64;
- usa assinatura ad hoc e verifica o bundle;
- não abre nem instala nada no Premiere.

Metal:

```sh
./scripts/build_metal.sh
```

Painel de desenvolvimento e CCX oficial do UDT:

```sh
./scripts/package_uxp.sh
./scripts/package_uxp_ccx.sh --replace
```

O primeiro comando produz um ZIP com checksum; o segundo usa o UXP Developer
Tool 2.2+ instalado para produzir o `.ccx`. A release final (ZIP UXP, CCX,
bundle nativo e checksums fechados) é criada por `./scripts/package_release.sh`
somente depois que o bundle nativo existir.

Instalação local, somente quando combinado e com o Premiere fechado:

```sh
./scripts/install_when_ready.sh --confirm
```

O instalador valida fonte, stage e destino, recusa execução sem `--confirm` e
se recusa a instalar enquanto Premiere Pro, PProHeadless, Adobe Media Encoder,
After Effects ou `aerender` estiverem abertos.
Se já houver uma instalação identificada como este produto, obtenha confirmação
explícita e use `--confirm --replace`; o bundle anterior fica em backup único.
Para remover de forma recuperável, com os hosts fechados:

```sh
./scripts/uninstall_when_ready.sh --confirm
```

### Carregar o painel local

Depois de instalar o bundle nativo com todos os hosts fechados, o fluxo
validado no Premiere 26.3 usa o UXP Developer Tool 2.2.1:

1. abra o Premiere com um projeto seguro;
2. no UXP Developer Tool, escolha `Add Plugin` e selecione
   `uxp/manifest.json` deste repositório;
3. selecione `Rosto Natural` na lista e escolha `Load`;
4. abra o painel `Rosto Natural` pelo menu de plugins/extensões do Premiere.

O `.ccx` também é gerado e validado como artefato de release, mas distribuição
externa exige a identidade e a assinatura apropriadas da Adobe. O gate real
desta versão carregou o painel pelo UXP Developer Tool.

### Uso

1. selecione exatamente um clipe de vídeo na sequência; o áudio vinculado pode
   permanecer selecionado junto;
2. abra o painel `Rosto Natural` e escolha `Aplicar efeito`;
3. ajuste intensidade, pele, textura, olhos, olheiras, tom, brilho, blush,
   lábios e cabelos rebeldes; `Mostrar original` alterna a comparação;
4. para estabilização temporal por malha facial, escolha `Analisar movimento`
   e aguarde a confirmação do cache;
5. `Restaurar preset` volta ao preset natural. Cabelos em `0` é bypass exato,
   `70` é o preset natural e `100` amplia a busca autorizada.

O analisador aceita sequências progressivas, pixels quadrados, velocidade
constante e até 7.200 quadros por execução. Trim, 0,5×, 1×, 2× e reverso têm
mapeamento explícito; rampas de velocidade falham fechadas e preservam o cache
anterior. A captura usa tokens HOLD correlacionados e lease de 10 segundos.
Sessões incompletas antigas são limpas apenas após validação estrita do
namespace temporário; conteúdo inesperado ou symlink é preservado, não seguido.
Esses invariantes passam nos testes automatizados. A API UXP 26.3 permite ler,
mas não alterar velocidade ou reverso; por isso a matriz de retiming não foi
forçada com API privada nem edição direta do projeto e permanece um gate
automatizado, não uma alegação de teste real no host.

## Validação no Premiere 26.3

Em projeto e mídia temporários seguros, o bundle nativo 0.11.0 foi reconhecido
e o efeito `AE.com.lucas.RostoNatural` foi aplicado sem travamento. Um gate
isolado começou com uma cadeia contendo somente Opacity/Motion e confirmou a
inclusão de exatamente uma instância pelo nome registrado `AE.`. O painel UXP
carregou, atualizou `Lábios` de 20 para 37 e a cor ao vivo, e o valor nativo 37
foi confirmado. Undo restaurou 20 e Redo restaurou 37.

O gate de quadro TGA 720×1280 confirmou que o preset facial altera somente a
região do rosto: 98.442 pixels, ou 10,6816% do quadro, com zero alteração nos
420 pixels superiores, 100 inferiores e 80 laterais. Cabelo em zero foi bypass
exato; 70 alterou 13.279 pixels (1,440864%) e 100 alterou 21.837
(2,369466%), sempre com zero pixels no rosto, cabelo protegido e fora da área
permitida.

A análise real processou e detectou rosto nos 210/210 quadros em 3,54 s. O
cache final contém somente `landmarks.bin` e `metadata.json`; captura, sessão e
token voltaram a zero, sem keyframes. Falhas induzidas antes da promoção
limparam suas sessões sem substituir o cache aprovado.

A exportação combinada de 7 s, 720×1280, 30 fps e 210 quadros terminou em
4,53 s no caminho CPU. Uma exportação original pelo mesmo preset de codec serviu
como controle; ambas decodificaram 210/210 quadros. Os quadros 0, 60, 120 e 209
foram inspecionados e mantiveram tratamento sutil, contorno estável e ausência
de deformação visível.

## Estrutura

- `core/`: imagem, máscaras, tratamento, cabelo, cache e estabilização;
- `mediapipe_bridge/`: ponte nativa Face Landmarker/Hair Segmenter;
- `metal/`: kernel facial;
- `adobe/`: efeito, bundle e PiPL;
- `uxp/`: painel;
- `tools/`: calibração, gates, CLIs e smoke tests;
- `models/`: modelos locais;
- `tests/`: testes C++ e JavaScript;
- `scripts/`: preparação, build, pacote e instalação protegida.

## Limite atual

As dependências estão atendidas com Adobe SDK 25.6, Xcode 26.6 build 17F113,
Metal Toolchain 17F109 e UXP Developer Tool 2.2.1. O bundle nativo foi
compilado, instalado e validado; o efeito e o painel já passaram pela primeira
aplicação, atualização ao vivo, análise e exportação no Premiere 26.3. Para uso
local no caminho implementado, os critérios do produto estão atendidos.

O runtime instalado continua CPU clássico de 8 bits: `.air` e `.metallib`
compilam, mas ainda não são carregados nem entram no bundle. Não há Smart
Render, float/deep color ou promessa de 4K fluido. A medição real acima mostra
tempo menor que o tempo real em 720×1280; a referência CPU offline 4K permanece
em aproximadamente 0,48–0,57 s por quadro.
