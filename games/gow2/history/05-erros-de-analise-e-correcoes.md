# Erros de análise (meus) e correções

Registro honesto de afirmações que estavam **erradas** e foram retiradas. O usuário valoriza candura: manter este
arquivo atualizado quando algo for retratado.

| # | Afirmação errada | Realidade | Como foi pego |
|---|---|---|---|
| E1 | "~38 mil primitivas por quadro" | contei vértices suprimidos por ADC=1; **~7,7 mil** triângulos são desenhados | exportador de triângulos passou a respeitar ADC |
| E2 | "GPU a ~5 ms" | latência escondida pelo rasterizador CPU; realista **14,6 ms** (`GT_MODE=gpuonly`) | medição isolada |
| E3 | "F8 do PCSX2 dá o quadro exibido" | em replay de dump devolve o menu sempre | testes com VRAM zerada e dump truncado |
| E4 | Exportador descartava vértices ADC da fila da tira | vértice entra na fila, só não *desenha* | comparação de contagens |
| E5 | Bench "0 fragmentos" | contador lido cedo | leitura de sanidade |
| E6 | "Composite do bloom (pacote 1216) é o defeito" | `p=152` está **dentro do Z-buffer** (zbp=120); a divergência nascia nos pacotes 21–22 | controle com 0 pacotes (erro 0,14) e bissecção |
| E7 | "FBMSK no domínio de 16 bits explica" | sem efeito no erro | experimento revertido |
| E8 | "Ordem/corrida, cache de textura da CPU, TEXA, screenshot do cabeçalho" como causas | todas descartadas por teste | `GT_SERIALIZE`, `GT_CPU_FLUSH_EACH`, etc. |
| E9 | "PCSX2 força bilinear (`filter=2`)" | artefato de instâncias acumuladas | recapturas isoladas |
| E10 | "Erro do quadro = 5" como se fosse do modelo | em boa parte era o piso do método (LANCZOS) | comparação exata mostrou 0,75 na cena |
| E11 | "O erro de ±1 do sprite vem de float impreciso" | era passo 16.16 truncado | sonda de rampa |
| E12 | "Três testes unitários brigam com o patch; precisam ser reescritos" | um deles lia a VRAM CT32 de forma **linear** (bug do teste) | `gs.ReadVram` no lugar |
| E13 | Estimativa "mais ou menos 90% do caminho" para o menu | só mede a metade superior de uma cena | revisão posterior; ver 07 |

## Padrão a evitar
1. Contar vértices em vez de primitivas desenhadas.
2. Citar tempo de GPU sem isolar a CPU.
3. Aceitar uma captura sem checar instâncias do emulador vivas.
4. Chamar de "erro do modelo" o que pode ser piso do método: **sempre** medir um corte de controle.
