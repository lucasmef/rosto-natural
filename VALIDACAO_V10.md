# Validação da v0.10 contra FaceTune

## Regra de aceitação

O FaceTune é a única referência visual deste projeto. Uma alteração no
algoritmo só pode ser aceita se `./scripts/validate_against_facetune.sh` passar.
O teste compara 42 quadros de 14 pares `cru / -facetune`.

A exportação do FaceTune altera levemente o quadro inteiro por recodificação.
Para não ensinar o plugin a copiar o codec, o validador estima uma transformação
de cor somente em regiões estáveis fora da face e a remove da referência. Nenhum
pixel do rosto é usado para aprender essa normalização.

## Resultado

Todos os 17 testes passaram:

| Grupo | Rosto Natural | FaceTune | Resultado |
|---|---:|---:|---|
| Microtextura | 0,712442 | 0,693072 | passou |
| Textura fina | 0,662486 | 0,634080 | passou |
| Textura média | 0,675828 | 0,664900 | passou |
| Nitidez da mandíbula | 0,989422 | 0,951763 | passou |
| Alteração fora do rosto | 0,016259 | 1,955649 | passou |
| Luminância da pele | -0,002275 | -0,001404 | passou |
| Crominância A da pele | -0,000185 | -0,000470 | passou |
| Crominância B da pele | -0,000458 | -0,000531 | passou |
| Luminância das olheiras | -0,016221 | -0,016237 | passou |
| Saturação dos olhos | -0,018220 | -0,018686 | passou |
| Luminância dos olhos | -0,003323 | -0,003977 | passou |
| Saturação dos lábios | +0,013932 | +0,013335 | passou |
| Luminância dos lábios | -0,004345 | -0,003449 | passou |

A distância direta de cor diminuiu 8,02% e a diferença estrutural diminuiu
15,21% em relação ao vídeo cru. Todos os 14 vídeos melhoram individualmente:
o pior ganho mediano é 2,77% em cor e 9,19% em estrutura.

## Verificação visual

- `artifacts/facetune-overlay-v10-final-65.jpg`
- `artifacts/facetune-overlay-v10-final-69.jpg`
- `artifacts/facetune-overlay-v10-final-71.jpg`
- `artifacts/facetune-overlay-v10-final-84.jpg`

Cada imagem contém cru, plugin, FaceTune, checker alternado e mapas de textura
amplificados quatro vezes.

## Limites desta validação

As imagens offline usam o fallback LBF de 68 pontos convertido para a malha de
478 pontos porque a sessão bloqueada do macOS não fornece o contexto necessário
ao MediaPipe. Ao desbloquear o Mac, a mesma bateria deve ser repetida com os 478
pontos reais, seguida do teste do shader Metal e do efeito dentro do Premiere.

A análise LBF não encontrou remodelagem consistente nos 70 pares de landmarks.
Isso ainda não prova que o FaceTune nunca remodela; a conclusão será repetida
com MediaPipe antes de decidir se um módulo geométrico é necessário.
