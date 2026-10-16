# Histórico de problemas — God of War II (SCUS_974.81) no PS2Recomp

Esta pasta registra **todos os problemas** encontrados ao longo do trabalho, para revisão futura: o que foi resolvido
(e como), o que foi contornado, o que está incompleto e o que continua aberto (com o impacto estimado).
O estado atual e os próximos passos ficam em [`../PROGRESS.md`](../PROGRESS.md); aqui ficam o *porquê* e o *como*.

## Índice

| Documento | Conteúdo |
|---|---|
| [01-timeline.md](01-timeline.md) | Linha do tempo, decisões do usuário e mudanças de rumo |
| [02-resolvidos-regras-do-gs.md](02-resolvidos-regras-do-gs.md) | Regras do GS que divergiam do PCSX2 e como foram provadas e corrigidas |
| [03-resolvidos-gpu-gs.md](03-resolvidos-gpu-gs.md) | Problemas ao implementar o GS na GPU (VRAM como buffer) |
| [04-resolvidos-oraculo-e-metodo.md](04-resolvidos-oraculo-e-metodo.md) | Problemas do oráculo PCSX2, da medição e do ambiente |
| [05-erros-de-analise-e-correcoes.md](05-erros-de-analise-e-correcoes.md) | Afirmações minhas que estavam erradas e foram retiradas |
| [06-contornados-e-incompletos.md](06-contornados-e-incompletos.md) | Soluções parciais, empíricas ou com ressalvas |
| [07-abertos.md](07-abertos.md) | Problemas não resolvidos: por que, quanto afetam, como atacar |
| [08-runtime-recompilador-e-infra.md](08-runtime-recompilador-e-infra.md) | Runtime, IOP, VU, build, repositório e dados |
| [09-armadilhas-e-licoes.md](09-armadilhas-e-licoes.md) | Armadilhas que custaram tempo e lições de método |

## Legenda

* **Resolvido**: causa entendida, corrigida e verificada por medição ou teste.
* **Contornado**: o sintoma some, mas a causa não foi eliminada ou há um desvio deliberado.
* **Incompleto**: corrigido em parte, ou a regra é empírica (reproduz as medidas sem explicar o mecanismo).
* **Aberto**: conhecido e não tratado.
* **Impacto**: *alto* = impede o objetivo (jogo rodando correto), *médio* = afeta a fidelidade em cenas ainda
  não medidas, *baixo* = diferença invisível ou só de ferramenta de teste.

## Estado em 2026-10-15 (resumo)

* O jogo chega ao menu, com controle, mas roda a ~3% da velocidade no runtime (o GS por software domina).
* Um GS em GPU (VRAM de 4 MiB como buffer, estilo paraLLEl-GS) existe e está validado contra o modelo em CPU,
  mas **não está ligado ao runtime**.
* Contra o PCSX2 (software renderer), o quadro do menu está a ~0,8 de erro médio por canal na metade de cima
  (medida exata), partindo de 11,6 no início do dia. A metade de baixo não foi medida com a mesma precisão.
* Só **uma cena** (menu principal, 4 quadros do mesmo dump) foi validada.
