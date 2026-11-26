---
name: nova-tarefa
description: Escreve a especificação de uma tarefa do orionpay-switch-adq (título, descrição, critérios de aceitação, módulos afetados, como validar). Use quando o usuário pedir para criar, detalhar ou especificar uma tarefa, issue ou história para o switch.
---

# Nova tarefa do orionpay-switch-adq

Escreva a tarefa em português usando exatamente as seções abaixo. Antes de preencher "Arquivos e Módulos Afetados", leia `src/` e cite só arquivos que existem de verdade, ou marque claramente os que serão criados. Se faltar uma informação essencial, como o objetivo ou o cenário de falha, pergunte ao usuário em vez de inventar.

## Título

Uma linha. Ex.: "Gerar desfazimento MTI 0420 no SAF quando a VisaNet der timeout"

## Descrição

O que precisa ser feito e por quê, do ponto de vista do switch: qual o ganho de latência ou resiliência, e qual problema de concorrência ou de protocolo está sendo resolvido no caminho quente.

Exemplo: tirar o insert em `switch.transactions` do fluxo síncrono e passá-lo para a worker thread do `PgWorker`, liberando a thread de rede logo após o recebimento do MTI 0110/0210.

## Critérios de Aceitação

Checklist. Inclua os itens abaixo que se aplicam e acrescente os específicos da tarefa:

- [ ] Compila sem erros nem warnings no perfil `Debug-WSL`.
- [ ] Recursos de rede e banco seguem RAII, sem vazamento nem conexão órfã.
- [ ] A thread de rede não bloqueia: HSM e Postgres continuam passando por filas e worker threads.
- [ ] A ISO 8583 respeita a especificação: bitmaps, BCD, LLVAR/LLLVAR.
- [ ] A gravação cai na tabela pai `switch.transactions`, na partição certa do `created_at`.
- [ ] Em queda de link ou timeout com a VisaNet, o SAF gera o MTI 0420.
- [ ] Nenhum PAN completo, PIN Block ou chave aparece em log.

## Arquivos e Módulos Afetados

Lista de arquivos em `src/` com uma linha dizendo o que muda em cada um.

## Como Validar

Passo a passo no ambiente local (WSL + `docker-compose.yml` + `hsm_simulator.py`). Por exemplo:

1. Subir o Postgres com `docker compose up -d postgres` e o simulador de HSM com `python3 hsm_simulator.py`.
2. Rodar o switch e disparar uma transação com `client_test.py` ou `tools/send_grpc.sh`.
3. Conferir no log do spdlog a tradução do PIN Block e o envio do MTI 0100/0200.
4. Derrubar o link com a VisaNet no meio do envio e confirmar que o MTI 0420 entrou no SAF.
5. Consultar `switch.transactions` no Postgres para confirmar a gravação.

## Contexto Adicional

Dumps hex de mensagens ISO, erros do libpqxx e logs relevantes fornecidos pelo usuário. Omita a seção se não houver nada.
