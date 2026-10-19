# F5 v004 · Metal local para o editor

Interface offline para `/CAMINHO/editor`. Não usa Adobe. Preset F5 v004 experimental, sem aprovação automática do preset ou do 174. A entrega é um processador externo de vídeo base, antes de compor legendas/stills. Não modifica o projeto editor, jobs, registros ou aprovações.

## O que executa na GPU

Conversão RGB8 → float, as duas bandas do único tratamento de pele, núcleo F5, restauração de olhos/abertura da boca/lábios, corretivo, cor labial e blend de visibilidade. Mesma ordem de quantização do CPU. Não há warp, nova suavização global ou geração de textura. O shader legado `metal/RostoNatural.metal` não é usado.

Tracking MediaPipe, estabilização, construção das máscaras exatas e verificação ficam na CPU. Decode/encode são FFmpeg. Portanto, é um pipeline híbrido; tempo do kernel não representa o tempo total. O contexto Metal é criado uma vez e buffers são reutilizados. Compila o shader em tempo de execução, sem precisar de `xcrun metal` ou instalação do Xcode completo. Exige macOS com GPU Metal disponível ao processo. Não há fallback CPU silencioso.

## Instalação local existente e diagnóstico

Use o Python do próprio rosto-natural: `.venv/bin/python`, com NumPy, OpenCV e MediaPipe 0.10.21 já instalados. Não é necessário instalar pacotes no editor. FFmpeg usado: `.local/bin/ffmpeg` deste repositório. A biblioteca local já foi construída em `build/libRostoMetalF5.dylib`.

```sh
cd /CAMINHO/rosto-natural
sh integrations/editor-metal-v1/build.sh
.venv/bin/python integrations/editor-metal-v1/runner.py doctor
```

O build usa clang++/Command Line Tools e frameworks locais Foundation/Metal. Não deve rodar simultaneamente com um render. Para rastreabilidade, o relatório registra hashes do binário, shader, preset e fonte. Rodar fora de um sandbox que bloqueie GPU/contexto gráfico. MediaPipe inicializa um contexto gráfico mesmo quando a inferência usa CPU/XNNPACK.

## Contrato de entrada

- Vídeo **base do corte atual**, sem textos/stills/overlays queimados; apenas um rosto. V1 trabalha com ProRes 422 progressivo, YUV422p10le/Rec.709, pixels quadrados, sem rotação pendente, CFR 60 fps desde zero, dimensões 16..4096. Até um tick de arredondamento de timestamps é aceito, limitado a 1/6000 s; VFR, lacunas e mudanças de fps não são corrigidas implicitamente.
- Áudio PCM estéreo 48 kHz. `pcm_f32le`, `pcm_s16le` e `pcm_s24le` são copiados no formato original. O 176 validado usa **PCM float32**, preservado byte a byte. Não converter float32 para PCM16 ao preparar a base. Vídeo sem áudio é recusado nesta v1.
- JSON de revisão específico para o hash/dimensões/contagem de frames desta entrada. Deve listar os cortes e as oclusões conhecidas. **Hands não detecta pompom**; região desconhecida exige bypass. Não reutilizar coordenadas/tempos do 176 em outro vídeo ou montagem.
- Arquivos de entrada e saída distintos. Diretório de trabalho novo, saída MOV nova; o runner recusa sobrescrita. A saída preserva a proporção e não recorta.

`review-176-v1.json` é um exemplo executável **somente para o 176 original com o hash registrado**. O review JSON é dado técnico de visibilidade, não aprovação estética/editorial. Para outra fonte, criar um arquivo com:

```json
{
  "sourceSha256": "SHA256_DA_BASE_EXATA",
  "width": 1440,
  "height": 2560,
  "frames": 600,
  "fps": 60,
  "cuts": [0, 300],
  "reviewed": true,
  "reviewNotes": "Descrever frames/intervalos realmente inspecionados e oclusões encontradas",
  "fadeFrames": 12,
  "spatialFeatherPx": 50,
  "rectangles": [{"frames": [40, 70], "box": [0, 1300, 850, 2560]}],
  "fullBypassIntervals": [[180, 200]],
  "reviewedHandOverrides": []
}
```

Exemplo estrutural: números fictícios, não executar sem revisão real. Frames usam intervalos [início, fim), zero no início da base. Retângulos usam pixels nativos dessa base, [x0,y0,x1,y1]; limites são conservadores. Feather apenas para fora da área insegura. O retorno temporal usa 12 frames e respeita os cortes. `fullBypassIntervals` desativa o rosto inteiro. `reviewedHandOverrides` deve ficar vazio normalmente: só admite um falso positivo de mãos já revisto e coberto por exclusão espacial explícita; nunca substitui tracking ausente/inválido.

## Executar num corte novo

```sh
cd /CAMINHO/rosto-natural
.venv/bin/python integrations/editor-metal-v1/runner.py prepare \
  --input /CAMINHO/base-corte-atual.mov \
  --review /CAMINHO/review-oclusoes.json \
  --work /CAMINHO/analise-F5-nova

.venv/bin/python integrations/editor-metal-v1/runner.py render \
  --work /CAMINHO/analise-F5-nova \
  --output /CAMINHO/base-F5-Metal-nova.mov \
  --size 1440x2560 --backend metal
```

Os paths `/CAMINHO` são placeholders. `prepare` analisa todos os frames, reinicia detectores nos cortes, estabiliza sem atravessar cortes/falhas e limita deslocamento de olhos/boca a 1 px do tracking. Esse limite não é erro medido contra ground truth. `render` confere hashes, gera MOV temporário e só publica a saída após verificar contagem/PTS/dimensões e todas as amostras PCM. Código de saída 0 significa verificação técnica concluída; 2 indica erro. Ler `base-F5-Metal-nova.report.json`, não inferir sucesso pela existência de arquivo parcial.

Use `--compare-cpu` na primeira amostra de cada integração para conferir cada frame contra CPU e as proteções exatas. Esse modo custa mais tempo. `--backend cpu` é fallback **explícito**, com o mesmo preset/contrato. Não misturar um novo arquivo tratado com áudio/vídeo de outra montagem.

`--reuse-v005 DIRETORIO` é apenas uma ponte de migração para o cache verificado do 176: exige source hash, dimensões, frames e hashes do manifesto v005. Para novos vídeos, omitir; não renomear cache do 176 para fazê-lo caber.

## Saída e integração no editor

Saída padrão 1440 × 2560, 60 fps, ProRes 422 Standard, YUV422p10le limitado, Rec.709. Processamento no tamanho da entrada; uma ampliação Lanczos se necessária. RGB8 intermediário: saída 10-bit não recupera precisão perdida. Não há linearização/tone mapping. Para reproduzir o 176 exatamente, processar o original 1080 × 1920 e ampliar depois; uma base já ampliada terá outra amostragem e exige revisão própria.

No editor, consultar `AGENTS.md`, `docs/HARNESS.md`, `scripts/studio.py` e estado do job. O ponto existente é:

```sh
cd /CAMINHO/editor
.venv/bin/python scripts/studio.py attach-finish JOB video /CAMINHO/base-F5-Metal-nova.mov \
  --notes 'Rosto Natural editor-metal-F5-v1; F5 v004; hashes/relatório; revisão temporal realmente realizada'
```

Executar somente para o job e corte correspondentes, dentro da etapa autorizada. Esse comando registra o arquivo e vínculo com a montagem; não executa o processador nem prova sozinho a qualidade/alinhamento. O tratado substitui apenas a base. Manter o fluxo de áudio/voz separado do editor, evitar reprodução duplicada e manter overlays editáveis. Reordenou/aparou cenas? Gerar nova base, review, análise e tratamento; não reutilizar o tratado completo anterior.

O agente do editor deve fazer uma integração em fixture/ambiente de teste antes de ligar a um job real; testar falha de GPU, fonte/corte alterado, saída já existente e alinhamento, e seguir `pnpm check`, `pnpm test:harness`, `pnpm status` e gates do projeto. Não preencher aprovações automaticamente nem contornar `render --master`/`release`. O 174 continua sem aprovação decorrente desta entrega.

## Testes e API

`test_parity.py DIRETORIO_NOVO`: 22 imagens nativas do controle 176, fixture 2K, regiões protegidas, bypass e entradas inválidas. Usa o controle CPU congelado. `test_contract.py`: valida vínculos de fonte, resolução, cortes e exclusões. Evidências da rodada completa em `local-dev/runs/2026-10-13-162102-metal-F5-editor-v1/`.

`engine.h` documenta a ABI C caso uma integração futura precise evitar subprocessos. Usar um contexto por worker serial. Não compartilhar contexto em chamadas concorrentes. Para esta integração, prefira o runner: ele também cuida de tracking, oclusão, cor, áudio e verificação. O wrapper `backend.py` não é um detector e exige malha atual e visibilidade explícitas.

Revisão por imagens e equivalência numérica não substituem playback contínuo de Lucas, especialmente no contorno do rosto, volta do bypass e movimento. Não se afirma igualdade estética com FaceTune nem tratamento automático seguro de qualquer novo objeto.
