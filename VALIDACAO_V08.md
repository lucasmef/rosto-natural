# Validação da v0.8

## Resultado visual

O preset v0.8 foi comparado com os 14 vídeos `-facetune` em 42 quadros. A
diferença mediana absoluta ficou abaixo de 0,0005 em todas as medidas de pele,
olhos e lábios, exceto no controle de olheiras, preservado como decisão visual.

Não há remodelagem facial. Diferenças de tamanho de olhos, nariz ou mandíbula
presentes no Facetune não são copiadas.

## Estabilidade

Teste: clipe 84, 120 quadros consecutivos, 60 fps.

| Medida | Resultado |
|---|---:|
| Quadros detectados | 120/120 |
| Oscilação bruta equivalente | 10,57 px |
| Oscilação estabilizada | 1,09 px |
| Redução de oscilação | 89,66% |
| Desvio mediano da trilha | 3,04 px |
| Desvio P95 | 23,20 px |

O P95 inclui rejeição deliberada de outliers do detector LBF. A trilha usa
liberação adaptativa durante movimentos rápidos para evitar atraso de máscara.

## Limitações desta rodada

- O fallback OpenCV LBF foi usado porque MediaPipe e Apple Vision não
  inicializam seus serviços gráficos enquanto a sessão do Mac está bloqueada.
- O tratamento CPU foi executado; o caminho Metal ainda não pôde ser carregado
  pelo Premiere.
- A integração Adobe não foi compilada por ausência do Xcode completo e do SDK.
