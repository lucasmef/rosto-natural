# Prompt para continuar o projeto Rosto Natural

Copie todo o texto abaixo ao criar uma nova tarefa do Codex vinculada à pasta
`/CAMINHO/rosto-natural`.

---

Trabalhe exclusivamente no projeto **Rosto Natural**, localizado em:

`/CAMINHO/rosto-natural`

Repositório privado:

`https://github.com/lucasmef/rosto-natural`

Não trabalhe em uma cópia improvisada e não misture este projeto com
`/CAMINHO/voz-neural`.

## Meta principal

Concluir e validar no Adobe Premiere Pro 26.3 um único produto local chamado
**Rosto Natural**, contendo dois módulos integrados:

1. retoque facial natural calibrado contra os resultados do FaceTune;
2. remoção de cabelos rebeldes, preservando o penteado principal, o rosto e o
   fundo fora da região autorizada.

O objetivo não é apenas manter um protótipo offline. Prossiga até que o efeito
nativo e o painel funcionem de verdade no Premiere, usando um projeto de teste
novo ou uma cópia segura, com validação visual e objetiva das exportações.

## Contexto comprovado

- Produto atual: `Rosto Natural v0.11.0`.
- Implementação-base preservada no commit `5fbd3f8`.
- O FaceTune é a referência visual aprovada; não copiar remodelagem geométrica
  de rosto, olhos, nariz ou mandíbula.
- O preset facial aprovado usa: Intensidade 72, Pele 58, Preservar textura 75,
  Uniformizar tom 24, Olheiras 42, Brilho 30, Olhos 18, Blush 0, Lábios 20.
- `Cabelos rebeldes` começa em 0 para bypass exato; 70 é o acabamento natural
  e 100 aumenta alcance e sensibilidade.
- A documentação registra 17/17 critérios faciais aprovados em 45 quadros de
  15 pares reais `cru / -facetune`.
- O módulo de cabelo registra zero alterações no rosto, no cabelo protegido e
  fora da área permitida no gate disponível.
- Após a mudança para esta pasta, passaram: 15 testes do núcleo C++, 1 teste
  do adaptador MediaPipe, 3 testes UXP e 11 verificações do contrato Adobe.
- A referência CPU 3840x2160 ficou em aproximadamente 0,48 segundo por quadro;
  desempenho fluido dependerá da integração Metal.

Os vídeos reais de referência ficam em
`/CAMINHO/referencias`. Eles são material pessoal e devem ser usados
somente para leitura e validação; nunca mover, renomear, sobrescrever, apagar ou
enviar esses arquivos ao GitHub ou a serviços externos.

## Arquitetura atual

- `core/src/retouch.cpp`: tratamento facial e preservação de textura;
- `core/src/flyaway.cpp`: análise e reconstrução de cabelos rebeldes;
- `core/src/temporal.cpp` e `core/src/cache.cpp`: estabilidade e cache;
- `mediapipe_bridge/`: Face Landmarker e Hair Segmenter;
- `metal/RostoNatural.metal`: caminho de GPU;
- `adobe/`: efeito nativo, PiPL e bundle;
- `uxp/`: painel do Premiere e aplicação dos parâmetros;
- `tools/`: calibração, comparações, gates e CLIs;
- `tests/`: testes determinísticos;
- `models/`, dependências, builds e artefatos: locais e ignorados pelo Git.

## Primeiras ações obrigatórias

1. Confirme `pwd`, branch, upstream e `git status`; preserve qualquer alteração
   que já pertença ao usuário.
2. Leia completamente `README.md`, `STATUS_PARA_LUCAS.md`,
   `VALIDACAO_V10.md`, `VALIDACAO_CABELO_V011.md`, `adobe/README.md`,
   `mediapipe_bridge/README.md`, `CMakeLists.txt` e os scripts relevantes.
3. Execute novamente, sem abrir o Premiere:

   ```sh
   ./scripts/preflight.sh
   ./scripts/build_core.sh
   ./scripts/build_cmake.sh
   PYTHONPYCACHEPREFIX=/private/tmp/rosto-natural-pycache \
     python3 tools/validate_adobe_contract.py
   ```

4. Não aceite automaticamente diagnósticos antigos. Depois da mudança de
   pasta, em 03/08/2026, o preflight registrou como pendentes: checkout do
   MediaPipe 0.10.35, ponte MediaPipe compilada, SDK C++ da Adobe, Xcode
   completo, compilador Metal e UXP Developer Tool. Verifique se são ausências
   reais, caminhos desatualizados ou caches/symlinks quebrados antes de baixar
   ou reinstalar qualquer coisa.
5. Preserve os caches e modelos locais. Não refaça downloads ou builds pesados
   se o estado existente puder ser recuperado com segurança.

Existe conteúdo em `.deps/mediapipe-0.10.35`, inclusive cache Bazel, mas o
diretório não possui mais `.git`. Isso pode fazer `prepare_mediapipe.sh` tentar
clonar dentro de uma pasta não vazia. Investigue e recupere esse estado sem
apagar a pasta ou o cache. Não use `git clean`, `reset --hard` ou outra limpeza
destrutiva.

No diagnóstico de 03/08/2026 ainda não existiam:

- `dist/native/libRostoMediaPipe.dylib`;
- o bundle `Rosto Natural.plugin`;
- o pacote `.ccx` final;
- a biblioteca Metal compilada.

O adaptador Adobe atual usa render clássico de 8 bits. Smart Render, float/deep
color e a ligação efetiva do shader Metal ao caminho Adobe ainda precisam ser
implementados ou ter sua limitação claramente medida e documentada.

## Sequência de implementação esperada

1. Corrigir o preflight e restaurar o checkout/cache local do MediaPipe sem
   perder o trabalho já realizado.
2. Concluir e testar `libRostoMediaPipe.dylib` com Face Landmarker e Hair
   Segmenter reais.
3. Resolver os pré-requisitos oficiais da Adobe e do Xcode/Metal, documentando
   exatamente o que foi instalado ou encontrado.
4. Compilar o núcleo, shader Metal e efeito nativo `com.lucas.RostoNatural`.
5. Gerar o bundle e o painel UXP sem incorporar mídias pessoais, relatórios ou
   dependências no Git.
6. Instalar somente para teste local, inicialmente com assinatura ad hoc. Não
   comprar nem exigir Apple Developer ID antes de comprovar o funcionamento;
   Developer ID é assunto de distribuição posterior.
7. Testar no Premiere Pro 26.3 usando projeto novo ou cópia descartável:
   aplicar o efeito a um clipe selecionado, ajustar parâmetros pelo painel,
   exportar quadros/trechos e comparar com o cru e o FaceTune.
8. Validar rosto e cabelo separadamente e juntos, incluindo estabilidade
   temporal, contorno, textura, ausência de vazamento e desempenho.
9. Atualizar documentação, instruções de instalação e status honesto.
10. Manter o repositório privado limpo e sincronizado, enviando somente código,
    testes, scripts e documentação.

## Regras de segurança e escopo

- Não abrir, alterar ou salvar sobre projetos pessoais do Premiere, inclusive
  o projeto `2026`; use projeto novo ou cópia explicitamente destinada a teste.
- Não fechar nem controlar Premiere, After Effects ou outros aplicativos se o
  usuário estiver utilizando o Mac; coordene antes de testes de interface.
- Confirme que Premiere e After Effects estão fechados antes de instalar ou
  substituir o efeito nativo. Faça backup e obtenha confirmação antes de
  substituir qualquer instalação existente.
- Não apagar dependências, modelos, artefatos, caches ou resultados existentes.
- Não modificar os arquivos pessoais usados como referência.
- Não desativar Gatekeeper, SIP ou outras proteções do macOS.
- Não instalar software de fonte não oficial.
- Não dê play em mídias pessoais sem necessidade; prefira exportar quadros ou
  pequenos trechos de teste.
- Não fazer engenharia reversa adicional de aplicativos proprietários sem
  necessidade concreta; a meta agora é terminar e validar nossa implementação.
- Não enfraquecer tolerâncias de validação apenas para fazer testes passarem.
- Se um teste falhar, determine a causa e preserve evidências antes de corrigir.
- Faça atualizações curtas de progresso durante trabalhos demorados.

## Critério de conclusão

Considere o projeto concluído somente quando:

- todos os testes atuais e os gates FaceTune/cabelo passarem;
- Face Landmarker e Hair Segmenter reais funcionarem no caminho nativo;
- o efeito aparecer, carregar e processar no Premiere Pro 26.3 sem travar;
- o painel UXP aplicar e atualizar os controles corretamente;
- o preset facial reproduzir a presença natural aprovada sem deformar o rosto;
- cabelo em 0 for bypass exato, 70 natural e 100 mais forte, sem alterar rosto,
  massa protegida ou fundo não autorizado;
- uma exportação real do Premiere tiver sido comparada e aprovada;
- o caminho GPU estiver validado ou a limitação de desempenho estiver medida e
  documentada de forma honesta;
- houver instruções claras de build, instalação, uso e desinstalação;
- nenhum projeto ou arquivo pessoal tiver sido modificado;
- o Git estiver limpo, privado e sincronizado.

Não pare apenas em planejamento ou diagnóstico enquanto houver uma próxima
ação segura dentro desse escopo. Se surgir um bloqueio que exija login,
instalação importante, controle do Premiere em uso ou decisão do usuário,
registre o estado exato e peça somente a intervenção necessária.

---
