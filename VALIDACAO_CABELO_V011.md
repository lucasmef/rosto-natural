# Validação de cabelos rebeldes — v0.11

## Objetivo

Remover fios finos fora do penteado principal sem suavizar o cabelo normal,
alterar o rosto ou reconstruir áreas não autorizadas do fundo.

## Pipeline

1. HairSegmenter gera a probabilidade de cabelo.
2. A massa principal é aberta e dilatada para formar a região protegida.
3. A busca ocorre somente fora dessa região.
4. Respostas direcionais separam fios curvos das linhas verticais do fundo.
5. No modo automático, a máscara é afinada para manter estruturas esparsas.
6. No modo guiado, um fechamento morfológico vertical de 71–101 px, controlado
   pelo slider, modela o fundo: dobras contínuas da cortina permanecem, enquanto
   interrupções escuras curtas ou curvas são removidas.
7. No modo guiado, a composição final é recortada exatamente pela região
   autorizada.

## Gates nativos atuais

Relatórios: `native_flyaway_analysis_gate.json` e
`native_flyaway_render_gate.json`.

| Verificação | 70% | 100% |
|---|---:|---:|
| Pixels na máscara de análise | 3.729 | 6.419 |
| Fração da análise | 1,981824% | 3,411458% |
| Pixels alterados no render | 10.813 | 17.325 |
| Fração do render | 1,434237% | 2,297989% |
| Energia de linha antes | 6,243503 | 5,725599 |
| Energia de linha depois | 4,397115 | 3,842655 |

Em zero, o render é bypass exato, sem pixel alterado. Em 70 e 100 houve zero
alterações no rosto, no cabelo protegido e fora da área permitida. A retenção
de cabelo forte foi 100% e a sobreposição entre as máscaras
permitida/protegida foi zero. A análise passou 6/6 verificações e o render
passou 5/5.

Hashes SHA-256 dos relatórios canônicos:

- análise: `13babc79045878797567deebd4a332b67176993814f1c4f64b998c362327cc59`;
- render: `ba2e327f275a221f9b2588866882a0bb8aa16c7748aafff77cb43d32aa2fea24`.

O relatório de análise agrega os níveis 70 e 100. O relatório canônico de
render e seu hash correspondem à execução final em 100; os números de bypass
0 e força 70 da tabela vieram das execuções anteriores preservadas da mesma
bateria, não desse único JSON.

## Slider de intensidade

- `0`: bypass exato;
- `1–69`: aumenta progressivamente a mistura da reconstrução;
- `70`: preset natural calibrado;
- `71–100`: amplia também a sensibilidade, a distância e o comprimento máximo
  para alcançar fios claros no topo.

## Proteção do escopo FaceTune

O controle `Cabelos rebeldes` começa em zero. A bateria facial foi repetida
depois da inclusão do módulo: os 17 testes contra o FaceTune continuam
passando nos 51 quadros dos 17 pares.

## Gate real no Premiere 26.3

O gate usou o mesmo quadro seguro TGA RGBA 720×1280 para força 0, 70 e 100,
com o preset facial fixo:

| Verificação no host | 70% | 100% |
|---|---:|---:|
| Pixels alterados | 13.279 | 21.837 |
| Fração do quadro | 1,440864% | 2,369466% |
| Delta máximo | 97 | 107 |
| Caixa alterada | `[60,419,663,1056]` | `[58,322,663,1054]` |

Em zero, o render foi idêntico ao quadro apenas facial. Em 70 e 100 houve zero
pixels alterados no rosto, no cabelo protegido ou fora da região permitida;
100 expandiu a cobertura em relação a 70. O gate passou 10/10 verificações.

A exportação completa com cabelo 70 e rosto aprovado produziu 210 quadros em
7 s e foi concluída em 4,529 s no caminho CPU. Os quadros 0, 60, 120 e 209
mantiveram a silhueta e o acabamento natural sem vazamento e não indicaram
cintilação nos quadros inspecionados. Assim, os gates offline e real no host
estão concluídos.
