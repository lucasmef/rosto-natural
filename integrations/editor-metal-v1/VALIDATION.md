# Validação local — 13/10/2026

Motor `editor-metal-F5-v1` implementado e executado no Apple M5 Pro. Preset F5 v004 preservado. Pronto para chamada externa pelo editor neste ambiente; **o conector do editor não foi implementado nesta rodada**. Nenhum job/checkpoint/aprovação do editor foi alterado. Nenhum envio de mídia, push, publicação ou instalação.

Evidências: `/CAMINHO/rosto-natural/local-dev/runs/2026-10-13-162102-metal-F5-editor-v1/`.

## Equivalência real

- 22 frames nativos do 176 contra o CPU congelado: todos RGB byte a byte iguais. Incluem boca aberta/fechada, cortes, pompom, virada de cabeça e último frame. Fixture 1440 × 2560 também passou no limite máximo de 1 nível RGB8.
- **176 completo: 1.343 frames**, Metal versus CPU antes da ampliação/codificação. 1.333 frames idênticos; nos outros 10, apenas **um canal de um pixel por frame** difere em **1/255**. Total: dez diferenças de um nível em todo o vídeo. Tolerância predefinida: máximo 1 nível, sem tolerância nas proteções totais.
- Zero alterações de tratamento nas proteções totais dos olhos/abertura da boca, realces labiais protegidos e regiões com peso zero. Isso é medido antes do codec/interpolação, não significa que ProRes preserve cada valor RGB original.
- As malhas, envelopes e exclusões reutilizados no teste completo são idênticos aos do controle v005. A comparação isola o backend. O decoder YUV do MOV difere do controle CPU anterior somente nos mesmos dez frames; não houve mudança de parâmetros/forma facial.
- Segunda execução completa sem comparação CPU: RGB processado e MOV final byte a byte iguais à execução validada.

Diferenças RGB ocorreram nos frames 143, 316, 467, 489, 839, 1034, 1175, 1181, 1199 e 1214. Compatíveis com quantização/arredondamento CPU/GPU; não se afirma identidade matemática universal em outros hardwares/compiladores. Rodar o teste de paridade após mudanças de ambiente.

## Vídeo e áudio

Master `176-F5-Metal-2K.mov`: ProRes 422 Standard, 1440 × 2560, 60 fps, 1.343 frames, 22,383333 s, SAR 1:1, Rec.709. Todos os frames decodificados, PTS e duração conferidos. Entrada nativa 1080 × 1920; única ampliação Lanczos após o tratamento.

PCM float32 estéreo 48 kHz: 1.074.400 amostras por canal, todas idênticas ao original. Não houve conversão de áudio no master. A prévia `176-Metal-preview.mp4` é 720 × 1280/AAC e não substitui o master de áudio.

SHA-256 do master: `198302c0773e0c78c8d2f36429802b39805ac5e5d39ac09dc66d3b166deb4b03`.

**Correção de metadados:** a verificação detectou que o antigo caminho de entrada RGB, também usado no master v005 anterior, não persistia as etiquetas Rec.709 apesar de executar a conversão de matriz declarada. Os primeiros renders Metal foram recusados por essa verificação e não publicados. O novo exportador aplica `setparams` explicitamente e `+write_colr`; tags foram confirmadas no MOV. O master v005 e seus relatórios históricos não foram alterados. Ao usar controles antigos, especificar a conversão de matriz na comparação; não assumir que o contêiner está etiquetado.

## Desempenho medido

| Medição | Resultado |
|---|---:|
| CPU, mediana por frame em 22 imagens | 83,26 ms |
| Metal, mediana incluindo máscaras/cópias por frame | 53,20 ms |
| Comandos GPU, mediana na mesma amostra | 7,24 ms |
| 176 completo: render + gravação, sem comparação CPU | 97,28 s |
| Chamadas de processamento acumuladas nessa execução | 71,41 s |
| Comandos GPU acumulados nessa execução | 8,37 s |
| Render completo com comparação CPU adicional | 220,50 s |

Tracking já calculado nas duas execuções completas. Tempos não incluem a verificação posterior de todos os frames/áudio nem review humano. O controle CPU v005 anterior levou 131,05 s de render/gravação, mas foi outra execução/pipeline de medição; não é benchmark controlado simultâneo. O ganho por frame da amostra foi cerca de 1,57×. Máscaras/tracking ainda usam CPU e há custo de cópias/codecs. Não é promessa de tempo real.

## Caminho de fonte nova e falhas

Fixture real de 60 frames, formada por dois trechos do 176 com corte no frame 30: análise MediaPipe nova, sem cache, reinício de detectores nos cortes; render Metal 2K comparado integralmente ao CPU. Zero diferença RGB, PCM idêntico. Executado com `MTL_DEBUG_LAYER=1`; a ferramenta confirmou Metal API Validation Enabled e concluiu sem erro de validação reportado.

- 15 testes do núcleo, 11 grupos de acabamento e 4 de complexion recompilados e passaram.
- 10 testes de contrato passaram: fonte, geometria, review, cortes, retângulos e bypass.
- Três testes da CLI real recusaram saída existente, source hash alterado e cache adulterado, sem publicar saída.
- API recusou mesh NaN/degenerada e visibilidade inválida; visibilidade zero devolveu a fonte exatamente.
- `doctor` fora do contexto GPU permitido falhou com código 2 e mensagem de indisponibilidade, sem fallback CPU silencioso. Execução desktop autorizada encontrou Apple M5 Pro e renderizou com Metal real.
- Sintaxe Python/shell e `git diff --check` passaram. A única alteração em arquivo preexistente desta rodada foi a exportação opt-in/read-only de grade de máscaras em `core/src/retouch.cpp`; o algoritmo legado não foi reescrito. Arquivos novos em `core/include/rosto/retouch_masks.hpp` e `integrations/editor-metal-v1`.

## Observação e limites

Foram inspecionados recortes nativos do resultado, incluindo o master 2K no frame 925; as diferenças numéricas acima não produziram uma diferença evidente nesse recorte. Não houve observação contínua quadro a quadro nem escuta crítica nova. A comparação completa é numérica; Lucas ainda deve avaliar continuidade, brilho, contornos e transições do bypass em playback.

O conjunto visual continua sendo o 176 e uma fixture derivada dele. Não certifica outra pessoa, luz, resolução, movimento, fechamento de pálpebras ou objeto novo. O JSON de oclusões do 176 não é um detector reutilizável. Uma base já ampliada em 2K é processada nessa resolução e pode ter outra textura; revisar a aplicação concreta. Nenhuma aprovação estética do F5 ou do 174 foi criada.

## Repetir e integrar

Comandos no `README.md`; prompt completo em `PROMPT_PARA_EDITOR.md`. `manifest.json` registra hashes de código/binário/preset/fontes de build; `dependency-versions.json` registra o ambiente local. Os `.report.json` dos renders registram fonte, parâmetros por hash, backend real, device, cor, áudio, comandos e tempos.

Para repetir a paridade de imagens sem sobrescrever evidências:

```sh
cd /CAMINHO/rosto-natural
.venv/bin/python integrations/editor-metal-v1/test_parity.py /CAMINHO/NOVO/paridade
```

Para um render completo com o cache já verificado, usar o diretório de análise `local-dev/runs/2026-10-13-162102-metal-F5-editor-v1/full` com `runner.py render`, indicando **outro nome** de saída e `--compare-cpu`. Não reconstruir a biblioteca no meio do trabalho: o hash dela pertence à análise; um novo build exige nova preparação.
