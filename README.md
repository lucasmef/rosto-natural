# Rosto Natural

Tratamento facial local para vídeo, com preservação de textura e sem alteração
intencional da geometria do rosto. O projeto reúne um núcleo C++20, um pipeline
experimental com Metal para um editor externo e um adaptador Adobe em desenvolvimento.

## Estado do projeto

- **Núcleo C++:** processamento de pele, máscaras, acabamento e estabilização temporal.
- **Integração Metal F5:** runner offline com tracking na CPU, tratamento na GPU e
  decode/encode com FFmpeg. O preset F5 v004 é experimental e exige revisão por vídeo.
- **Adobe Premiere Pro:** adaptador e painel UXP presentes; a instalação e a validação
  completa no host continuam pendentes.

A integração mais recente está documentada em
[integrations/editor-metal-v1/README.md](integrations/editor-metal-v1/README.md).
Ela tem um shader próprio e não usa o shader Metal legado da frente Adobe.

## Começar pelo núcleo

Em macOS, com Command Line Tools e Node.js disponíveis:

```sh
./scripts/build_core.sh
python3 tools/validate_adobe_contract.py
```

Alternativamente, com CMake 3.24 ou superior e compilador C++20:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

A integração de vídeo requer Python 3.12, NumPy 1.26.4,
OpenCV contrib 4.11.0.86, MediaPipe 0.10.21, FFmpeg e GPU Metal.
O clone não inclui `.venv`, FFmpeg local, modelos, SDKs ou mídias de referência.
As instruções da integração descrevem o ambiente local anteriormente utilizado;
prepare essas dependências antes de executar seu diagnóstico.

## Privacidade e versionamento

Este repositório é privado. Credenciais, ambientes virtuais, builds, mídias pessoais
e relatórios locais ficam fora do versionamento pelo `.gitignore`.
Em `local-dev/`, somente as duas pontes C++ necessárias ao build da integração
são versionadas; os experimentos e suas evidências permanecem no computador.
Os exemplos usam caminhos genéricos, que devem ser substituídos no ambiente local.
Nunca adicione arquivos `.env`, chaves ou vídeos ao commit.

## Referência histórica: frente Adobe v0.11

Os resultados e números abaixo são registros da validação anterior, não uma
nova medição nem uma garantia de desempenho ou aprovação do pipeline F5.


Efeito local de aprimoramento facial, maquiagem leve e acabamento de cabelos
rebeldes para Adobe Premiere Pro. A v0.11 foi calibrada usando os 15 pares
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
- painel UXP 0.11 e adaptador de efeito com match name
  `com.lucas.RostoNatural`;
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

São avaliados três quadros de cada um dos 15 pares reais, totalizando 45
amostras. A comparação remove somente a deriva global da recodificação,
estimada fora do rosto. Qualquer tratamento local continua visível na métrica.

| Medida | Rosto Natural | FaceTune |
|---|---:|---:|
| Microtextura | 0,715541 | 0,703905 |
| Textura fina | 0,655024 | 0,645249 |
| Textura média | 0,668634 | 0,671587 |
| Nitidez da mandíbula | 0,989505 | 0,949602 |
| Alteração fora do rosto (0–255) | 0,015938 | 1,968870 |
| Luminância da pele | -0,002251 | -0,001298 |
| Luminância das olheiras | -0,016058 | -0,015642 |
| Saturação dos olhos | -0,021290 | -0,020720 |
| Luminância dos olhos | -0,003421 | -0,003707 |
| Saturação dos lábios | +0,013080 | +0,012964 |
| Luminância dos lábios | -0,003580 | -0,003284 |

O tratamento ficou 7,98% mais próximo do FaceTune em cor e 15,15% em
estrutura do que o vídeo cru. O pior dos 15 vídeos ainda melhorou 2,82% em cor
e 9,18% em estrutura. O portão tem 17 critérios e falha se qualquer tolerância
for ultrapassada.

Relatórios:

- `facetune_validation_gate.json`
- `facetune_semantic_gate.json`
- `facetune_contour_gate.json`
- `facetune_texture_gate.json`
- `facetune_similarity_gate.json`

Execute novamente com:

```sh
./scripts/validate_against_facetune.sh "/CAMINHO/referencias"
```

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

Resultados registrados na validação anterior:

- 15 testes do núcleo C++;
- 1 teste do adaptador C++/MediaPipe;
- 3 testes UXP;
- 11 verificações do contrato Adobe;
- referência CPU 3840×2160: aproximadamente 0,45–0,59 s por quadro.

O caminho CPU é a referência de equivalência. A reprodução fluida em 4K
dependerá da ligação final do shader Metal ao GPU Device Suite.

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

O build usa no máximo quatro tarefas e grava detalhes em
`build-mediapipe.log`. Ele foi pausado para não competir com o trabalho atual;
o cache já contém a maior parte das etapas.

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

Painel de desenvolvimento:

```sh
./scripts/package_uxp.sh
```

Esse comando produz um ZIP de desenvolvimento com checksum. O `.ccx` oficial
deve ser produzido pelo UXP Developer Tool.

Instalação local, somente quando combinado e com o Premiere fechado:

```sh
./scripts/install_when_ready.sh --confirm
```

O instalador recusa execução sem `--confirm` e se recusa a instalar enquanto o
processo do Premiere estiver aberto.

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

## Limites registrados na frente Adobe

Todos os gates offline passam. Ainda faltam o SDK Adobe, o Xcode completo, o
UXP Developer Tool, a conclusão da biblioteca MediaPipe e o primeiro
carregamento real no Premiere. Até esse teste no host, o projeto é uma
implementação offline validada, não um plugin instalado.
