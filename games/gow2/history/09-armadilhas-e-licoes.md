# Armadilhas e lições de método

## Armadilhas (cada uma custou tempo)
1. **Instâncias do PCSX2 acumuladas** mostram o quadro de um dump anterior. Sempre matar por linha de comando e
   esperar; desconfiar de qualquer resultado "bom demais" ou "igual ao anterior".
2. **`pkill -f`/`pgrep -f ... | kill`** casam com a linha de comando da própria ferramenta e matam o shell.
3. **`TBP0` está em blocos** (página = 32 blocos); `FBP/ZBP` em páginas. Ler Z24 como textura exige `tbp = zbp*32`.
4. **`p=152` está dentro do Z-buffer** (zbp=120, Z24 ocupa as páginas 120–232). O jogo reutiliza essa memória como
   buffer de blur; imagens "do Z" e "do blur" se misturam.
5. **Dump truncado** deixa `PRMODECONT=0`: forçar 1 para o blit.
6. **`gpu0.ppm` é a imagem da GPU**, não da CPU. Para iterar só no modelo CPU use `gow2_gs_replay` (`SYN_CPU=1`).
7. **O PCSX2 não mostra o `DISPFB` alterado**: copiar para o buffer original com sprite.
8. **Janela do PCSX2**: posição e escala mudam com o gerenciador de janelas; calibrar pela posição.
9. **CT32 em VRAM não é linear**: ler com o acessor com swizzle, nunca por `(y*64+x)*4`.
10. **GLSL**: divisão não é IEEE, `precise` obrigatório, tabelas grandes em SSBO, cuidado com FMA.
11. **Teste aleatório não cobre dados reais**: o `draw_test` passava com 16 000 prims enquanto o quadro real
    divergia (auto-realimentação, sprites de tamanho zero...). Ter sempre um teste sobre quadro real.
12. **`kill_all` por nome amplo matou o PCSX2 do usuário** (2026-10-16): casava `pcsx2-v2.9` em qualquer instância.
    Corrigido para casar só `-datapath /tmp/pcsx2-exact-data`. Regra: ferramenta de automação nunca mata por padrão amplo.
13. **`printf %02d` com 08/09 falha** (octal) em shell: nomes de saída trocados e arquivos sobrescritos; uma grade de
    resultados saiu com cenas misturadas. Nomear saídas pelo horário do dump, nunca por contador formatado.
14. **O screenshot do PCSX2 é 1024x603 já filtrado** (display filter): comparar com a saída crua 512x448 dá
    "serrilhado" que não é erro do GS. Reduzir por média de área antes de comparar.
15. **Replay de dump no PCSX2 pára em um quadro aleatório do dump** ("Dump Frame: N / Packet a/b"): sem escolher o
    quadro, a comparação exata mede o quadro errado. `hud_exact.py` ficou experimental por isso.
16. **Dumps tirados durante FMV têm 1–2 primitivas** (vídeo MPEG decodificado + sprite): não validam rasterização.
    Gameplay de verdade: 70–290 mil primitivas por 3 quadros.
17. **Start (Return) apertado em gameplay abre a pausa**: roteiros de teclas devem parar de enviar Start após o FMV.

## Lições de método
* **Sempre medir um controle** (corte 0, textura conhecida) antes de atribuir erro ao modelo.
* **Uma característica por sonda**, comparada pixel a pixel; usar texturas cujo conteúdo *revela* o parâmetro
  (rampa `R=16·x`, grade `blend_grid`) em vez de ruído.
* **Bissecção por truncamento** do dump localiza o primeiro pacote que diverge; `GT_FIRSTDIFF` faz o mesmo
  entre CPU e GPU.
* **Ler o código do PCSX2** quando a medição aponta o ponto exato; confirmar com sonda depois.
* **Passos curtos, commit e push a cada peça; relatar o que *não* foi provado**.
* **Test-first no modelo**: a sonda vira teste de regressão do comportamento.
* **Método do usuário: montar o quebra-cabeça, peça a peça.** Primeiro criar cada peça nativa e medir contra a referência
  (PCSX2/interpretador); só depois refinar. **Não** transformar a peça em caça a um defeito do jogo (levei bronca por isso
  em 16/10). Meta de aceitação da peça: >=95% igual ao original.
* **Unidade de tradução do VU1 é o segmento (hash do conteúdo), não a "imagem":** só há uma memória de código; traduzir 142
  imagens inteiras custava 1 min e 2 GB cada. Por segmento de 64 pares: 173 funções, build em 47 s com -j12 e cache.
* **Testar o interpretador exige invalidar o cache de decodificação** (passar `memory=nullptr` ou bumpar a geração).
* **Flags/Q/P do VU não bloqueiam leitura e têm latência** (FMAC 4 ciclos): código do jogo lê o MAC "velho" no mesmo par.
  Escrita imediata dá 0% idêntico; o rastro da sequência de pares localizou o ponto exato.
* **Entradas patológicas (registradores saturados) não servem para comparar semântica** com o PCSX2; usar o estado do
  próprio PCSX2 como entrada sã.
* **Injeção em savestate do PCSX2:** TPC + bit 8 de VU0.VI[29] + start_pc, VIF1 TOP/ITOP no `eeHwRegs.bin` (0x3CE0/0x3CD0),
  EE em `b .` para o jogo não sobrescrever a memória do VU1; MTVU e flag hack desligados no diretório privado.
* **`/tmp` é apagado no reinício:** dados do oráculo e resultados em `~/gow2`, não em `/tmp`.
* **SSH porta 22 pode ficar bloqueada:** `GIT_SSH_COMMAND='ssh -p 443 -o HostName=ssh.github.com -o HostKeyAlias=github.com' git push ...`
  (usa a chave já confiável de github.com; não altera configuração nem aceita chave nova).


## Otimizar pelo perfil sem medir o custo por unidade (2026-10-16)
* O `perf` mostrou `vu1n::fmac` com 12,8% da CPU e parecia o alvo óbvio. Duas tentativas (inline forçado de `fmac`/`op_*`;
  caminho rápido sem conta em `double` quando o resultado é normal, 152.000 casos ainda idênticos) deram **0% de ganho**
  no benchmark (23,2 µs/chamada antes e depois).
* Conta (corrigida depois de medir por fase dentro do jogo): eu tinha estimado ~5 ns por par a partir de UM caso de 4.700
  pares; era errado. No jogo, cada chamada gasta ~25 µs só executando o programa (preparação 0,3 µs, escrita 0,06 µs,
  entrega ao GS 1,2 µs) e faz ~800 ciclos: **~30 ns por par**. O custo está na execução gerada (confirmação de flags,
  scoreboard), não na ponte nem na conta em `double`.
* Regra: antes de otimizar uma função do perfil, dividir (tempo total ÷ chamadas) e comparar com o custo teórico.


## Achar a origem de um valor ruim: assinatura + primeira operação que o cria (2026-10-16)
* O fundo preto vinha de NaN nas matrizes. Em vez de perseguir o VU1 (que estava correto), **comparei uma assinatura com a
  referência**: palavras `0xFFC00000` na memória do VU1, 0 no PCSX2 e 127 por chamada nas nossas. Isso apontou que o valor
  entra como bits crus, ou seja, vem do EE (o VU1 não cria NaN na própria aritmética).
* Depois **instrumentei só a primeira operação que cria NaN/Inf a partir de entradas finitas** e guardei o endereço do
  chamador (`__builtin_return_address`, offset em relação a uma função conhecida, `nm` para o nome). Em um único run saíram as
  funções do jogo exatas e as operações (SQRT.S de negativo, VMUL com estouro).
* Regra do hardware conferida no PCSX2 antes de implementar (ler para entender, reimplementar); teste da peça (30 casos) e
  a mesma medição depois (107.319 -> 0).
* **Macros que avaliam o argumento duas vezes** (ao envolver `FPU_ADD_S(a, b)` num checador) quebram com expressões
  aninhadas e custam; trocar por funções inline `always_inline`.

## Comparar velocidades quando a carga muda (2026-10-16)
* Depois do conserto do fundo, a mesma configuração ficou **mais lenta** (12,0 -> 6,0 vsync/s) porque o GS agora desenha o
  fundo inteiro e o VU1 faz menos chamadas, maiores. Tabelas de velocidade precisam dizer **qual carga** foi medida; os
  ganhos de cada peça só se comparam dentro da mesma carga (build, cena, variáveis).
* Medir por fase (preparação, execução, escrita, entrega) antes de otimizar; a ponte parecia suspeita e custava 6% do total.
* `perf annotate` pode travar em binários grandes: usar `perf script -F ip,sym` + histograma de endereços e `objdump`.
* Com a máquina livre, `ninja -j<núcleos>` sem `nice`; reiniciar o build é barato (objetos prontos ficam).


## Defeito visual 3D: seguir os dados até a origem, não o componente que "parece" culpado (2026-10-20)
* O fundo do menu estava errado e a hipótese natural era textura, GS ou VU1. **Nenhuma estava certa.** O que funcionou foi
  medir contra a referência em níveis: (1) o que o GS recebe (triângulos por quadro e por textura, nosso x PCSX2); (2) o que o VU1
  emite (vértices com ADC=1); (3) o que o VU1 recebe (a linha de projeção na memória de dados); (4) quem escreve esse buffer (DMA
  chain -> endereço de RDRAM -> watchpoint de escrita -> função do jogo -> código dela).
* **Watchpoint de escrita do EE (`PS2_WATCH`)** com `ra` e registradores `s*` do escritor dá a função e o objeto (`s1`) em uma
  única rodada; mais barato do que ler código no escuro.
* **Quando a semântica "à mão" dá o resultado certo e o binário dá errado, desconfie da tradução de instrução**, não do
  jogo. Conferir cada macro/tradutor de instrução contra a definição lane a lane com um teste que **falha com o código antigo**
  (provar que o teste pega o defeito).
* Instruções que mexem em lanes: lembrar que `_MM_SHUFFLE(d3,d2,d1,d0)` lista os índices do destino do mais alto para o mais
  baixo; erros de ordem são silenciosos e só aparecem em dados específicos.
* **Binário desatualizado engana:** depois de mudar header, código gerado e `.cpp`, conferir o horário do binário e rodar
  uma sonda barata (ex.: `PS2_EE_DUMP`) antes de tirar conclusão de uma imagem. Não confundir "ainda igual" com "não funcionou".
* Valor da observação do usuário: o painel preto torto era pista de que o defeito era geométrico/global (a matriz), não do fundo.

## Testar o tradutor pelo caminho real, contra a definição, e não confiar em "já funciona" (2026-10-20)
* Depois de achar 6 shuffles MMI errados, a auditoria do resto achou mais 34 instruções erradas e 1 que derrubava o processo.
  Quase todas compilavam e "pareciam" vetoriais corretas; o erro estava na semântica (HI/LO de 128 bits, lanes 0 e 2,
  operandos trocados, saturação).
* O teste certo passa a **palavra da instrução** pelo decodificador e gerador reais e compila o C++ emitido. Testar só as
  macros deixa de fora o registro errado (`PABSW` lia rs) e o que o tradutor escreve à mão. Uma cópia das macros
  (`mmi_macros_extract.inc`) pode divergir do header.
* Referência escalar escrita do manual, conferida com uma segunda fonte (interpretador do PCSX2) antes de declarar um
  defeito. Dois falsos positivos meus apareceram assim: encodei `rd` em instruções que não têm `rd` (`PDIV*`).
* Medir o impacto antes de comemorar: contar onde o jogo usa cada instrução (contagem no ELF tem dados misturados;
  o `diff` do código regenerado é a medida real). Aqui o impacto no GoW2 foi pequeno (4 locais `PMULTH`).
