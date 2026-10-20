# Linha do tempo e decisões

Datas: o trabalho descrito aqui ocorreu em 2026-10-15 (sessões longas, com compactações de contexto).
Para os commits anteriores, ver `git log` das branches `gow2` e `gow2-gpu-gs`.

## Ponto de partida
* PS2Recomp recompila EE -> C++ (estático). O jogo (God of War II USA) já inicializava e chegava a um menu
  correto com entrada de controle, a ~3% da velocidade.
* VU1 e GS eram **interpretados**. No menu o gargalo era o GS por software (~650 ms/quadro), não o VU1.

## Decisões e direções do usuário (em ordem)
1. **Problema principal declarado:** recompilar VU1 e VU0 corretamente e rodá-los em paralelo.
2. **"Semi-emular":** usar o que roda bem num emulador (VUs e GS), recompilar depois só o VU0, depois o VU1 e por
   fim o GS, para ter uma forma **visível de validar** cada peça. Resultado: o PCSX2 virou **oráculo**.
3. **Leitor de dump do GS** (formato do PCSX2) para reproduzir exatamente o que o PCSX2/PS2 faz.
4. **"Tudo referente à imagem deveria ser tratado pela GPU; a CPU não deveria decodificar a textura."**
   -> desenho "nível 1": VRAM inteira de 4 MiB como buffer da GPU, texturas decodificadas no sombreador.
5. Testar e **comparar desempenho incluindo o deslocamento de dados via PCIe** (medido: não é o gargalo).
6. Trabalhar numa **branch separada** (`gow2-gpu-gs`) para não comprometer o que já funciona; **sempre dar push**
   para o fork depois de cada peça.
7. **"Queria ver imagens ou ver tudo rodando, pra ter certeza de que está certo"** -> comparações lado a lado
   (referência | CPU | GPU | diferença) sempre que possível.
8. **Passos pequenos e limitados**, sem rodar algo muito tempo sem relatar; criticou tentativa e erro ("gohorse"):
   exige auditoria estática primeiro, teste antes de corrigir e verificação por medição.
9. Direção estratégica (fim do dia): **entender como o PCSX2 roda em tempo real e recriar isso nativo**; ler o
   código do rasterizador do PCSX2 para formar hipóteses e confirmar com sondas sintéticas.

## Marcos técnicos
* Formato do dump GS decodificado e validado (marcador, cabeçalho, blob de estado, regs privilegiados, pacotes).
* Tabelas de endereçamento da VRAM geradas do código C++ e usadas no GLSL (13,6 M leituras, 0 divergências).
* GPU GS por etapas: upload/swizzle, CLUT, pipeline por pixel, primitivas (80 mil prims, 0 divergências),
  quadros reais (menu) CPU == GPU salvo 10 words explicadas.
* Oráculo PCSX2 automatizado (X aninhado, captura de janela) + bissecção por truncamento do dump.
* Regras do GS achadas (ver 02): centro do pixel inteiro, Z truncado, UV de sprite sem +0,5, borda top-left,
  bilinear de 4 bits truncado, passo de UV 16.16, cobertura de sprite por `ceil`.
* Erro do quadro final do menu vs PCSX2: 11,6 (início) -> 0,8 (fim), metade superior.

## Commits relevantes (branch `gow2-gpu-gs`)
`612b7fd` CLUT na GPU · `7260556` pipeline por pixel · `5501cb1` primitivas · `53a599d` quadros reais ·
`c0c85a2` oráculo automatizado · `b6fcfa1` sondas Z/isolinha · `f320836` centro inteiro + Z truncado ·
`26b832e` UV de sprite · `8a2b83a` borda top-left · `7f50203` ferramenta de comparação exata ·
`8de9dfd` sprite exato PCSX2 + bilinear 4 bits + instantâneo de textura.

## 2026-10-16
* Dumps novos: 4 cenas (menu de dificuldade, FMV x3) e 19 de gameplay (4–10 min, combate no Colossus) em
  `~/gow2/pcsx2-ref/{scenes,gameplay}`; usuário aprovou visualmente os 18 pares PCSX2xGPU (`gameplay/pares`).
* Usuário notou serrilhado/alpha/cores diferentes: serrilhado = screenshot do PCSX2 filtrado (1024x603); barras iguais;
  alpha do contador não confirmado (medição exata falhou por quadro desalinhado).
* Profiling do jogo vivo (`perf`): VU1 ~36%, GS CPU ~22%, escalonador IOP ~12%.
* GPU ligada ao runtime (`PS2_GS_BACKEND=gpu`), commit `ed66930`. Fundo do menu continua ausente: causa fora do GS (A16).
* Erro meu: `kill_all` matou o PCSX2 do usuário; corrigido (`88e4786`).

### 2026-10-16 (tarde/noite): VU1 nativo
* Capturados 142 estados da memória de código do VU1 (2 núcleos fixos + trechos trocáveis); entradas reais (845 chamadas).
* Semântica nativa (superior e inferior) validada por instrução contra o interpretador: 152.000 casos, 100%.
* Gerador de C++ por segmento + despachante: 845/845 chamadas idênticas, 5,5x mais rápido.
* Oráculo PCSX2 para VU1 por injeção em savestate: 6/6 idênticos (interpretador e nativo). Entradas capturadas do nosso
  runtime divergem só por estarem saturadas (A20), não por erro do VU1.
* Erros meus: voltei a caçar a causa de um defeito quando o método é montar peças (corrigido); recompilei 1 imagem por
  vez (custo); `/tmp` perdido no reinício (dados do oráculo passaram a `~/gow2`).


### 2026-10-16 (noite): VU1 nativo ligado, IOP, FPU do EE, fundo do menu
* VU1 nativo ligado ao runtime (plugin `dlopen`), verificação ao vivo 98,8% (MAC final diverge, A21). Menu: 2,0 -> 3,5 -> 9,5 vsync/s (nativo, GPU).
* Agendador da IOP: 3-4 varreduras de `std::map` por fatia viraram um vetor contíguo: 9,5 -> 12,0 vsync/s.
* Duas tentativas de otimizar `fmac` sem ganho; medição por fase mostrou que o custo é a execução gerada (~30 ns por par).
* Pista A20 virou causa: NaN `0xFFC00000` (107 mil palavras) vindo do EE; `SQRT.S` de negativo e `VMUL` do VU0 com IEEE puro.
  Peça `ps2_ee_fp.h`: NaN 107 mil -> 0, fundo do menu deixa de ser preto (commit `bcae549`). Velocidade na nova carga: 1,0 / 1,5 / 3,0 / 6,0.
* Erros meus: duas hipóteses de otimização erradas (inline e caminho rápido do `fmac`) e uma conta de 5 ns por par a partir
  de um único caso (corrigida); `perf annotate` travou no binário grande (usei histograma de endereços); compilei com `-j8`
  e `nice` com a máquina livre (reiniciei com `-j12`).

### 2026-10-20: o menu passa a parecer o do PCSX2 (instruções MMI mal traduzidas)
* Textura do fundo decodificada da VRAM no instante do draw (`gs_tex_dump`): a 512x512 T4 era uma cornija aditiva e o rosto do
  Kratos vem de 3 texturas T8 512x512 (`tfx=3`) cujos draws quase não existiam na nossa execução (0 contra ~1.600 por quadro).
* Comparação com o PCSX2: ~9.600 triângulos por quadro contra ~5.000 nossos; faltavam só as camadas 512x512. O VU1 emitia todos
  os vértices dessas camadas com ADC=1 porque a linha de projeção chegava com z/w=0.
* Cadeia até a causa: origem do buffer no DMA chain (`0x569890`) -> watchpoint de escrita (`PS2_WATCH`) -> função `0x1787E0`
  -> semântica de `PEXEW`/`PROT3W` no recompilador estava errada. Correção nas macros e nos tradutores MMI; teste `mmi_test` 6/6.
* O usuário notou que o painel preto da frente tinha perspectiva torta ("efeito Star Wars") junto do fundo quebrado: era o
  mesmo defeito (a mesma matriz). Boa pista de quem olha a imagem e não o código.
* Erros meus: levei algumas rodadas com binários parcialmente recompilados (mudança só em `.cpp` não mostra o efeito da
  mudança no header/gerado: sempre conferir o horário do binário e a saída de uma sonda antes de concluir); suposição errada
  de que o dump do PCSX2 era 1 quadro (eram 4 vsyncs); apaguei sem querer arquivos em `/tmp` de novo (usar `~/gow2/logs`).

### 2026-10-20: auditoria completa das instruções MMI (A23 resolvida)
* Ferramenta `tools/mmi_audit`: decodificador + gerador reais contra referência escalar do manual (conferida com o interpretador
  do PCSX2). 74 ok / 34 errados / 1 que derrubava o processo -> 109/109. Suíte do projeto `ps2x_tests` 438/0.
* No GoW2 só 4 locais `PMULTH` mudaram; menu igual e mesma velocidade. O ganho é eliminar uma classe inteira de defeitos
  silenciosos antes que apareçam em gameplay (física, áudio, animação).

