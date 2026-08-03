# Rosto Natural v0.11 — status para o Lucas

Data: 28/07/2026

## Resultado atual

O tratamento facial offline está aprovado contra o FaceTune: **17 de 17
critérios passaram em 45 quadros de 15 pares reais** `cru / -facetune`. As
tolerâncias não foram afrouxadas.

- nitidez da mandíbula: 0,9895 do cru, contra 0,9496 no FaceTune;
- alteração média fora do rosto: 0,0159 níveis de cor;
- microtextura: 0,7155, contra 0,7039 no FaceTune;
- textura fina: 0,6550, contra 0,6452;
- textura média: 0,6686, contra 0,6716;
- olhos, lábios, tom e olheiras dentro das tolerâncias FaceTune;
- resultado 7,98% mais próximo do FaceTune em cor e 15,15% em estrutura do que
  o vídeo cru;
- todos os 15 vídeos melhoraram individualmente nas duas medidas;
- nenhuma remodelagem geométrica é aplicada.

O removedor de cabelos rebeldes também está implementado no núcleo C++:

- slider contínuo de 0 a 100; zero é bypass exato;
- 70 mantém o acabamento conservador; 100 amplia alcance e sensibilidade;
- HairSegmenter separa massa principal e franja exterior;
- reconstrução respeita o fundo e protege estruturas verticais;
- na foto real, somente 2,30% do quadro mudou;
- zero pixels alterados no rosto, no cabelo protegido ou fora da região
  permitida;
- energia dos fios na área tratada caiu de 5,73 para 3,84.

## O que já está preparado

- 15 testes determinísticos do núcleo C++;
- 1 teste da ponte C++/MediaPipe;
- 3 testes do analisador UXP;
- portão de contrato Adobe com 11 verificações;
- Face Mesh de 478 pontos e máscaras semânticas;
- código da ponte nativa estável para Face Landmarker + Hair Segmenter;
- MediaPipe 0.10.35 e Bazel 7.4.1 oficiais, fixados e baixados localmente;
- painel UXP 0.11 com ajustes ao vivo e `Cabelos rebeldes`;
- recurso PiPL, modelos e biblioteca MediaPipe previstos no bundle;
- compilação, empacotamento e instalador com proteções;
- instalador exige confirmação e se recusa a rodar com o Premiere aberto;
- pacote de desenvolvimento UXP com checksum em `dist/`.

## Etapa pesada pausada

A biblioteca MediaPipe chegou a 5.260 de aproximadamente 5.740 etapas, mas foi
pausada para não disputar CPU enquanto o Lucas trabalha. O cache foi preservado.
A retomada agora usa no máximo 4 núcleos e grava o log em arquivo, evitando
sobrecarga visual do WindowServer:

```sh
ROSTO_BUILD_JOBS=4 ./scripts/prepare_mediapipe.sh
```

Depois da compilação, o teste real da ponte é:

```sh
PYTHONPATH=.python-legacy python3 tools/test_mediapipe_bridge.py \
  /caminho/para/foto.jpg
```

## Etapas e dependências ainda necessárias

Estas etapas não foram executadas e nenhuma delas exige mexer no projeto aberto
no Premiere:

1. baixar o SDK C++ atual da Adobe após login no Developer Console;
2. instalar o Xcode completo, necessário para compilar o shader Metal;
3. instalar o UXP Developer Tool 2.2+ pela Creative Cloud;
4. retomar a biblioteca MediaPipe quando o Mac estiver ocioso;
5. gerar o bundle nativo e o `.ccx`;
6. somente depois, com o Premiere fechado ou em horário combinado, instalar e
   testar a integração.

Uma identidade Apple paga não é necessária para teste local: o script usa
assinatura ad hoc. Ela só será necessária para distribuição externa.

## Limite honesto

O algoritmo, a comparação FaceTune, o cabelo rebelde, o painel e o caminho de
build estão prontos e testados fora do host. O plugin ainda não pode ser chamado
de concluído no Premiere até obter o SDK Adobe, terminar MediaPipe/Metal, gerar
o bundle e fazer o primeiro teste dentro do Premiere Pro 26.3.

Nenhum comando desta preparação abriu, fechou, controlou ou modificou o
Premiere.
