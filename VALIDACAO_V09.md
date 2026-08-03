# Validação da v0.9

## Contorno

Teste: 42 quadros dos 14 pares reais, resolução de avaliação de 960 px.

| Medida | Rosto Natural | Facetune |
|---|---:|---:|
| Nitidez mediana da mandíbula | 97,16% | 95,50% |
| Alteração média fora do rosto | 0,011 | 2,111 |
| Alteração P95 fora do rosto | 0 | 5 |

A v0.8 preservava apenas 80,82% da nitidez e alterava em média 4,77 níveis
fora da face. A v0.9 usa o trajeto real da mandíbula do detector, máscara
exclusivamente interna, proteção de bordas RGB e um segundo degradê interno
mais largo para a suavização.

## Aparência

O preset padrão usa intensidade 72, pele 58 e preservação de textura 25. Tom,
crominância, olhos e lábios foram recalibrados com uma máscara de avaliação que
não inclui cabelo ou fundo.

A razão de textura mediana ainda difere do Facetune (0,724 contra 0,547). Parte
da variação vem da aproximação LBF de 68 pontos; alguns clipes recebem cobertura
de máscara diferente. A decisão final desse parâmetro depende da repetição com
MediaPipe de 478 pontos.

## Remodelagem

Foram comparadas 70 amostras cruas/tratadas. Nenhuma medida de largura de
mandíbula, olhos, nariz ou boca apresentou simultaneamente magnitude e direção
consistentes. O efeito v0.9 não remodela o rosto. A conclusão sobre o que o
Facetune faz será confirmada com MediaPipe antes de decidir se um módulo de
remodelagem é necessário.

## Estabilidade

Em 120 quadros consecutivos a 60 fps:

| Medida | Resultado |
|---|---:|
| Quadros detectados | 120/120 |
| Redução de oscilação | 89,66% |
| Desvio mediano da trilha | 3,04 px |
| Desvio P95 | 23,20 px |

O teste temporal e as imagens offline ainda usam o fallback LBF porque a sessão
bloqueada do macOS não fornece o contexto gráfico exigido pelo MediaPipe.
