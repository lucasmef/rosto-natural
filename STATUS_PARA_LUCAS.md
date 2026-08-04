# Rosto Natural v0.11 — status para o Lucas

Data: 03/08/2026

## Resultado atual

O tratamento facial offline continua aprovado contra o FaceTune: **17 de 17
critérios passaram em 51 quadros de 17 pares reais** `cru / -facetune`. As
tolerâncias não foram afrouxadas. Em 03/08/2026 o agregado foi recalculado a
partir dos quatro relatórios existentes; os vídeos pessoais de origem não
foram relidos.

- nitidez da mandíbula: 0,9897 do cru, contra 0,9485 no FaceTune;
- alteração média fora do rosto: 0,0155 níveis de cor;
- microtextura: 0,7155, contra 0,7062 no FaceTune;
- textura fina: 0,6405, contra 0,6350;
- textura média: 0,6529, contra 0,6630;
- olhos, lábios, tom e olheiras dentro das tolerâncias FaceTune;
- resultado 7,98% mais próximo do FaceTune em cor e 15,15% em estrutura do que
  o vídeo cru;
- todos os 17 vídeos melhoraram individualmente nas duas medidas;
- nenhuma remodelagem geométrica é aplicada.

O removedor de cabelos rebeldes também passa nos gates nativos:

- slider contínuo de 0 a 100; zero é bypass exato;
- análise em 70: 3.729 pixels, ou 1,981824% da imagem de análise;
- análise em 100: 6.419 pixels, ou 3,411458%;
- render em 70: 10.813 pixels alterados, ou 1,434237%, com energia de linha
  reduzida de 6,243503 para 4,397115;
- render em 100: 17.325 pixels alterados, ou 2,297989%, com energia reduzida de
  5,725599 para 3,842655;
- retenção de cabelo forte em 100% e sobreposição permitida/protegida zero;
- zero pixels alterados no rosto, no cabelo protegido ou fora da região
  permitida;
- análise 6/6 e render 5/5 passaram.

## Dependências e build

As dependências oficiais necessárias estão disponíveis:

- Adobe After Effects SDK 25.6 em `vendor/AfterEffectsSDK`;
- Xcode 26.6, build 17F113;
- Metal Toolchain 17F109;
- Adobe UXP Developer Tool 2.2.1;
- MediaPipe 0.10.35 e Bazel 7.4.1, com o checkout e o cache locais preservados.

A ponte nativa real foi compilada e validada:

- Face Landmarker retornou 478 pontos finitos e normalizados;
- Hair Segmenter retornou máscara de probabilidade válida;
- a ponte e as oito dylibs OpenCV são arm64, assinadas ad hoc e publicadas
  juntas com dependências e rpaths validados;
- o build mantém XNNPACK CPU e desativa SME/SME2 incompatíveis com este Mac.

Validações automatizadas atuais:

- 16 testes determinísticos do núcleo C++;
- 1 teste da ponte C++/MediaPipe;
- 12 testes do analisador UXP;
- 9 testes da ponte UXP/Premiere;
- 18 verificações do contrato Adobe;
- benchmark CPU offline 3840×2160: aproximadamente 0,57 segundo por quadro.

## Bundle, instalação e release

O bundle nativo 0.11.0 foi compilado, assinado ad hoc, validado e instalado em
`~/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore`. O binário
instalado é idêntico ao binário atual em `dist/native`, e a validação estrita de
assinatura passa.

PiPL, modelos Face Landmarker/Hair Segmenter, ponte MediaPipe e dependências
OpenCV estão realmente presentes no bundle. Os instaladores recusam operação
sem confirmação ou com Premiere Pro, PProHeadless, Adobe Media Encoder, After
Effects ou `aerender` abertos; substituição exige `--replace` e preserva backup
recuperável.

A release 0.11.0 foi regenerada após as correções descobertas no host. Os
checksums atuais são:

- UXP ZIP: `3cd42e9b888071bb1094a27e930a9641be6c4ae9fd1d88b978f340a42929e136`;
- Native ZIP: `97ba5aa1f0851f05eb7c0d567524c16b8f829d5f8e7ad4cbb91e45ff1643ccd0`;
- CCX: `321f40f317ccc863349dc8771c725d0d7a90f219d67c224ea3ca00c544c8101b`;
- `SHA256SUMS`: `97a5a5dfe466a2417070196d76d17ea9a7f26da9bbfbd6a0772471d44c1325c1`.

`SHA256SUMS`, o sidecar UXP, os ZIPs e o CCX passaram na validação. O CCX é o
pacote local produzido pelo UDT, sem identidade de distribuição externa.

## Validação concluída no Premiere Pro 26.3

O gate usou somente o projeto temporário
`/private/tmp/RostoNatural_Validation_20260803_184500.prproj` e o clipe seguro
de 7 segundos `/private/tmp/rosto-natural-safe-gate-7s.mp4`, SHA-256
`bbec6d62c2aed7b781ac9ad59925c1b875b229031be97fad7d07ff62518981d9`.

Resultados confirmados:

- Premiere 26.3 reconheceu o efeito como `AE.com.lucas.RostoNatural`;
- numa sequência isolada criada do clipe seguro, a cadeia começou apenas com
  Opacity/Motion e recebeu exatamente uma instância de `Rosto Natural`, sem
  travamento; a sequência original foi reativada e o projeto seguro foi salvo;
- o painel UXP carregou e ficou com status de ajustes ao vivo;
- `Lábios` mudou de 20 para 37 e a cor para `#102030`;
- o valor nativo 37 foi lido de volta e confirmado;
- Undo restaurou o valor nativo para 20 e Redo restaurou 37;
- o preset aprovado foi restaurado e salvo somente no projeto temporário.

O Premiere 26.3 registra e cria o efeito com o prefixo `AE.`, devolve os nomes
`Lábios` e `Cor dos lábios` com mojibake e não permite ler parâmetro Color por
`getValueAtTime`. O painel agora usa o nome registrado na fábrica, canonicaliza
a cadeia e esses aliases e usa `ppro.Color` para escrever a cor. Os testes
automatizados incluem a aplicação sobre uma cadeia inicialmente limpa.

### Rosto e cabelo exportados

O gate de quadro real usou TGA RGBA 720×1280 no tempo 3,5 s:

- o preset facial alterou 98.442 pixels, ou 10,6816% do quadro, com delta
  absoluto médio de 0,1468 por canal e máximo 11;
- a caixa alterada foi `[206, 594, 556, 1005]`, restrita à face; as faixas
  superior de 420 px, inferior de 100 px e laterais de 80 px ficaram intactas;
- cabelo 0 foi idêntico ao quadro facial;
- cabelo 70 alterou 13.279 pixels (1,440864%), caixa
  `[60, 419, 663, 1056]`;
- cabelo 100 alterou 21.837 pixels (2,369466%), caixa
  `[58, 322, 663, 1054]`;
- nos níveis 70 e 100 houve zero alteração no rosto, no cabelo protegido e
  fora da região autorizada; 100 ampliou a cobertura como esperado.

### Análise, cache e falhas seguras

A execução final analisou 210/210 quadros, detectou rosto em todos e gravou
malha versão 2 em 3,54 s. A pasta promovida contém somente:

- `landmarks.bin`, 806.416 bytes;
- `metadata.json`, com 210 quadros, 30 fps, pixels 1:1 e tempos do host.

No efeito, `_Analysis ID` corresponde ao cache, `_Cache Version=2`,
`_Capture Mode=false`, `_Capture Session=0` e `_Capture Token=0`; os cinco
parâmetros ficaram sem keyframes. As tentativas que falharam durante o
desenvolvimento foram recusadas antes da promoção e suas sessões incompletas
foram removidas. A lease não permanece depois do sucesso. O gate não forçou
unload/crash. A proteção é baseada na expiração da lease, e a coleta segura de
sessões expiradas possui cobertura determinística.

### Vídeo completo, estabilidade e desempenho

A exportação combinada com rosto aprovado + cabelo 70 produziu um MP4
720×1280, 30 fps, 7 s, 210 quadros, SHA-256
`d0553a0cb42f210e1fb12cd110186d79dbed1c533f474a19bebb8fa8c51b37c6`.
Terminou em 4,529 s; a exportação original de controle pelo mesmo codificador
levou 0,448 s. Ambas decodificaram 210/210 quadros.

Comparando as duas exportações, o delta absoluto médio por quadro teve mediana
0,952, p95 1,042 e máximo 1,076 níveis por canal. A mudança temporal do delta
teve mediana 0,485, p95 0,642 e máximo 1,226. A inspeção dos quadros 0, 60,
120 e 209 confirmou tratamento sutil e estável, sem deformação ou vazamento
visível.

A API pública UXP 26.3 só lê velocidade/reverso; a permissão de controle visual
do macOS recusou abrir o diálogo de velocidade. Trim, velocidades constantes,
reverso e recusa de rampa continuam cobertos pelos testes determinísticos, mas
não são apresentados como uma matriz real de host. A matriz real de retiming
não foi executada e não integra as alegações de host desta release.

## Limite honesto e segurança

O shader Metal compila, mas `.air`/`.metallib` ainda não são carregados pelo
render nem incorporados ao bundle/ZIP nativo. O runtime instalado permanece no
caminho CPU clássico de 8 bits, sem Smart Render, float/deep color ou promessa
de 4K fluido.

O plugin está compilado, instalado, carregado, aplicado, analisado e exportado
no Premiere. Para o produto local CPU clássico de 8 bits, os critérios de
conclusão estão atendidos. A limitação Metal/4K permanece explícita e medida,
não mascarada.

O projeto pessoal `2026` e a pasta `/Users/lucas/Movies/Vídeos/Edit` não foram
abertos, alterados ou relidos nesta fase. Somente o projeto temporário acima foi
salvo. Nenhuma mídia pessoal entrou nos pacotes nem no Git.
