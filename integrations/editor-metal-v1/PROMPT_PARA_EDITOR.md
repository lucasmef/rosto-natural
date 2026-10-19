Integre o Rosto Natural com Metal ao projeto `/CAMINHO/editor`, como processador local de vídeo base. Não usaremos Premiere/Adobe. Implemente e teste o conector; não entregue só um plano.

Comece conferindo branch, remoto, alterações locais, `AGENTS.md`, `docs/HARNESS.md`, `docs/INTEGRACOES.md` e o estado relevante do editor. Preserve o trabalho existente, originais, evidências, jobs, checkpoints e aprovações. Faça a integração e os testes em fixture/ambiente de teste antes de anexar a um job real. Não trate o 174 como aprovado.

O motor já está implementado em:

`/CAMINHO/rosto-natural/integrations/editor-metal-v1/`

Leia `README.md`, `VALIDATION.md`, `runner.py`, `dependency-versions.json` e `review-176-v1.json`. Use o runner e o Python do rosto-natural, sem copiar a versão antiga presente em `editor/analysis/face-validation/rosto-natural` e sem instalar dependências no editor. O backend chama Metal de verdade; tracking/estabilização/máscaras permanecem na CPU. Os parâmetros são F5 v004, experimentais, sem aprovação estética automática. Não implementar outro filtro nem aumentar a suavização.

Diagnóstico:

```sh
cd /CAMINHO/rosto-natural
.venv/bin/python integrations/editor-metal-v1/runner.py doctor
```

A biblioteca já está em `build/libRostoMetalF5.dylib`; se houver necessidade de recompilar, use `sh integrations/editor-metal-v1/build.sh` e prepare uma análise nova. O shader é compilado em tempo de execução: não exige Xcode completo ou `xcrun metal`. GPU/contexto gráfico precisam estar disponíveis ao processo macOS; se o sandbox negar, execute no contexto local permitido. Não simule sucesso nem faça fallback CPU silencioso.

Implemente no editor a chamada `prepare` seguida de `render`, documentadas no README. Use subprocesso com lista de argumentos, sem interpolação de shell; verifique retorno, relatório, hashes e erros. A entrada deve ser a **base do corte atual, sem legendas/stills/overlays queimados**, com ProRes 422 progressivo, YUV422p10le, Rec.709, pixels quadrados, 60 fps desde zero e PCM estéreo 48 kHz. Preserve PCM float32 quando essa for a fonte; não reduza para PCM16 ao gerar a base. Resolução padrão de saída: 1440 × 2560, sem recorte. O processamento ocorre na resolução da entrada; o 176 de referência foi tratado em 1080 × 1920 e ampliado uma vez depois.

Cada base/corte exige review JSON vinculado ao SHA-256, dimensões, frames, cortes e oclusões efetivamente inspecionadas. Use bypass explícito nas regiões inseguras. Hands não detecta pompom. Não reutilize coordenadas, intervalos, cache ou o override de mãos do 176 em outro job/montagem. Para novos vídeos, omita `--reuse-v005`. O tracking novo reinicia nos cortes; olhos/abertura da boca/dentes/língua e realces labiais têm proteções próprias. Isso não dispensa revisar continuidade e objetos novos.

Use `--backend metal`; `--compare-cpu` na primeira fixture compara cada frame, embora acrescente tempo. `--backend cpu` existe apenas como escolha explícita. Saídas e diretórios de análise devem ser novos. A CLI retorna 0 só depois de verificar todos os frames e o PCM. Leia o `.report.json` associado, que identifica o backend real, device, fonte, preset, binário, shader, tempos e hashes. Não deduza sucesso apenas pela existência de um arquivo.

O ponto atual do editor é `studio.py attach-finish JOB video CAMINHO --notes ...`: ele associa a base tratada ao corte e preserva overlays editáveis. Não basta chamar attach-finish; prepare a base correta, execute o motor, verifique duração/enquadramento/fps/frames/cor/áudio e revisão temporal antes de anexar dentro da etapa autorizada. Mantenha a voz tratada pelo fluxo de áudio do editor separada, sem duplicá-la ou substituí-la pelo áudio do vídeo por engano. Se a ordem/corte mudar, invalide e refaça o acabamento.

Teste sucesso real com Metal, indisponibilidade de GPU, fonte/corte alterado, cache adulterado e tentativa de sobrescrita. Registre o que foi comparado por pixels e o que depende de playback humano. Siga `pnpm check`, `pnpm test:harness`, `pnpm status` e os gates do editor; não contorne `render --master`/`release` nem preencha aprovações por inferência. Não envie mídia para serviços externos, não faça push/publicação e não instale Adobe.

Entregue o conector implementado, testes/resultados reais, comandos para repetir e uma prévia de fixture com procedência completa. Preserve a versão CPU como controle. Consulte `VALIDATION.md` para os limites e as métricas reais; não prometa tempo real com base apenas no tempo do kernel GPU.
