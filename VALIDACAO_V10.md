# Validação da v0.10 contra FaceTune

## Regra de aceitação

O FaceTune é a única referência visual deste projeto. Uma alteração no
algoritmo só pode ser aceita se `./scripts/validate_against_facetune.sh` passar.
O teste compara 51 quadros de 17 pares `cru / -facetune`.

A exportação do FaceTune altera levemente o quadro inteiro por recodificação.
Para não ensinar o plugin a copiar o codec, o validador estima uma transformação
de cor somente em regiões estáveis fora da face e a remove da referência. Nenhum
pixel do rosto é usado para aprender essa normalização.

## Resultado

Todos os 17 testes passaram:

| Grupo | Rosto Natural | FaceTune | Resultado |
|---|---:|---:|---|
| Microtextura | 0,715541 | 0,706220 | passou |
| Textura fina | 0,640518 | 0,634980 | passou |
| Textura média | 0,652850 | 0,662981 | passou |
| Nitidez da mandíbula | 0,989655 | 0,948513 | passou |
| Alteração fora do rosto | 0,015519 | 2,014436 | passou |
| Luminância da pele | -0,002251 | -0,001298 | passou |
| Crominância A da pele | -0,000163 | -0,000554 | passou |
| Crominância B da pele | -0,000436 | -0,000573 | passou |
| Luminância das olheiras | -0,016058 | -0,015642 | passou |
| Saturação dos olhos | -0,021593 | -0,020790 | passou |
| Luminância dos olhos | -0,003425 | -0,003101 | passou |
| Saturação dos lábios | +0,012897 | +0,012508 | passou |
| Luminância dos lábios | -0,003626 | -0,003195 | passou |

A distância direta de cor diminuiu 7,98% e a diferença estrutural diminuiu
15,15% em relação ao vídeo cru. Todos os 17 vídeos melhoram individualmente:
o pior ganho mediano é 2,82% em cor e 9,18% em estrutura.

Em 03/08/2026 o agregado 17/17 foi recalculado a partir dos quatro relatórios
existentes, cobrindo as mesmas 51 amostras. Os vídeos pessoais de origem não
foram relidos nessa execução.

## Verificação visual

- `artifacts/facetune-overlay-v10-final-65.jpg`
- `artifacts/facetune-overlay-v10-final-69.jpg`
- `artifacts/facetune-overlay-v10-final-71.jpg`
- `artifacts/facetune-overlay-v10-final-84.jpg`

Cada imagem contém cru, plugin, FaceTune, checker alternado e mapas de textura
amplificados quatro vezes.

## Limites e repetição no host

Este gate reproduzível ainda usa o fallback LBF de 68 pontos convertido para a
malha de 478 pontos. Em separado, a ponte MediaPipe real já passou com 478
pontos finitos/normalizados e máscara Hair Segmenter válida. Em separado, o
comportamento do preset foi validado sobre uma exportação real do Premiere,
comparada ao controle cru seguro. Esse gate adicional não repete nem substitui
a comparação FaceTune canônica de 51 amostras.

Essa repetição foi concluída em um clipe temporário seguro no Premiere 26.3.
Num TGA RGBA 720×1280, o preset alterou 98.442 pixels (10,6816%), com delta
absoluto médio 0,1468 por canal e máximo 11. A caixa de mudança
`[206, 594, 556, 1005]` ficou sobre a face; as faixas superior de 420 px,
inferior de 100 px e laterais de 80 px permaneceram idênticas. O mapa de
diferença amplificado confirmou olhos, pele e lábios sem fundo alterado.

A exportação completa tratada e o controle original, ambos produzidos pelo
mesmo preset H.264 do Premiere, decodificaram 210/210 quadros. Nos quadros 0,
60, 120 e 209 a presença permaneceu natural, sem mudança geométrica ou
instabilidade visível. Isso complementa, sem substituir, o gate FaceTune
canônico de 51 amostras.

A análise LBF não encontrou remodelagem consistente nos pares de landmarks.
Isso não prova que o FaceTune nunca remodela. Independentemente disso, o
produto atual deliberadamente não aplica remodelagem geométrica, e o gate real
do Premiere não mostrou deformação.
