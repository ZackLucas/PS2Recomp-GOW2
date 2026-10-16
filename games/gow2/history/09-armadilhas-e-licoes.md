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

## Lições de método
* **Sempre medir um controle** (corte 0, textura conhecida) antes de atribuir erro ao modelo.
* **Uma característica por sonda**, comparada pixel a pixel; usar texturas cujo conteúdo *revela* o parâmetro
  (rampa `R=16·x`, grade `blend_grid`) em vez de ruído.
* **Bissecção por truncamento** do dump localiza o primeiro pacote que diverge; `GT_FIRSTDIFF` faz o mesmo
  entre CPU e GPU.
* **Ler o código do PCSX2** quando a medição aponta o ponto exato; confirmar com sonda depois.
* **Passos curtos, commit e push a cada peça; relatar o que *não* foi provado**.
* **Test-first no modelo**: a sonda vira teste de regressão do comportamento.
