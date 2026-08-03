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

## Gate da imagem enviada

Arquivo: `flyaway_validation_gate.json`.

| Verificação | Resultado |
|---|---:|
| Pixels selecionados em 100% | 38.438 |
| Fração do quadro em 100% | 5,0984% |
| Mudanças fora da vizinhança da máscara | 0 |
| Mudanças dentro da proteção do rosto | 0 |
| Mudanças fora da região guiada | 0 |
| Pixels da máscara fora da região guiada | 0 |
| Sobreposição com o cabelo protegido | 0 |
| Redução da energia das linhas em 100% | 28,3996% |

Todos os seis testes passaram.

O preset natural de 70% continua disponível: seleciona 23.410 pixels
(3,1051%) e reduz a energia das linhas em 34,9466%. A energia percentual é
menor em 100% porque o denominador passa a incluir também os fios residuais
claros, de baixa energia, que antes não faziam parte da máscara.

## Slider de intensidade

- `0`: bypass exato;
- `1–69`: aumenta progressivamente a mistura da reconstrução;
- `70`: preset natural calibrado;
- `71–100`: amplia também a sensibilidade, a distância e o comprimento máximo
  para alcançar fios claros no topo.

## Proteção do escopo FaceTune

O controle `Cabelos rebeldes` começa em zero. A bateria facial foi repetida
depois da inclusão do módulo: os 17 testes contra o FaceTune continuam
passando nos 42 quadros dos 14 pares.

## Limite atual

O protótipo de análise e as reconstruções C++ automática e anisotrópica estão
implementados. A ligação do
HairSegmenter ao render do efeito e o teste da máscara nativa do Premiere
dependem do SDK Adobe e do Mac desbloqueado. O filtro temporal adaptativo da
máscara já está no núcleo e possui teste determinístico; falta alimentá-lo com
quadros reais dentro do host.
