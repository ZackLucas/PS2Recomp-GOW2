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

