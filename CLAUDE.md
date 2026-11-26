# orionpay-switch-adq

Switch adquirente da OrionPay: o motor que faz a autorização em tempo real, no caminho quente, a perna Base I. Escrito em C++20.

## Fluxo de uma transação

1. O `merchant-service` (Java) chama `AuthorizeTransaction` via gRPC (`src/grpc/acquirer.proto`, servidor em `src/grpc/grpc_server.cpp`).
2. A requisição é convertida em ISO 8583 (`src/iso8583`, `src/mapper`).
3. O PIN Block é traduzido de Terminal Key para ZPK no HSM (`src/crypto/hsm_worker.cpp`). O protocolo com o HSM é TCP com prefixo de tamanho de 2 bytes big-endian e comandos `CT`/`RT`.
4. A mensagem é enviada à bandeira por TCP persistente (`src/network/visanet_client.cpp`). O link é mantido com eco MTI 0800.
5. A transação é gravada em `switch.transactions` no PostgreSQL por uma worker thread (`src/database/pg_worker.cpp`). A tabela é particionada por range diário de `created_at`.
6. Se houver timeout com a bandeira, o SAF (`src/saf`) gera e reenvia o desfazimento MTI 0420 em background.

A especificação original (protocolo do HSM, DDL da tabela) está em `docs/especificacao-inicial.md`.

## Build

- Ambiente: Linux/WSL. No CLion, use o perfil `Debug-WSL`.
- `cmake -S . -B build && cmake --build build -j`
- Exige CMake 3.28 ou mais novo.
- Dependências do sistema: Protobuf, gRPC, librdkafka (`rdkafka++` via pkg-config), OpenSSL, PostgreSQL.
- Dependências baixadas via FetchContent: spdlog, Asio standalone, nlohmann_json, libpqxx.
- Todo `.cpp` novo precisa ser adicionado à lista do `add_executable` no `CMakeLists.txt`.
- O código gerado do gRPC vai para `build/generated_grpc_files`. Não edite esses arquivos.

## Execução local

- `docker-compose.yml` sobe Postgres, Kafka e Kafka UI.
- `hsm_simulator.py` simula o HSM na porta 15001.
- Para testes: `client_test.py`, `adquirer_client_example.py` e os scripts em `tools/`.
- A configuração é lida de `config.json`, que está no `.gitignore` porque contém credenciais. Use `config.example.json` como modelo. Nunca commite o `config.json` nem coloque credenciais, chaves ou IPs direto no código.

## Regras de código

- **RAII sempre.** Use `std::unique_ptr`/`std::shared_ptr` e feche sockets e conexões de banco de forma determinística.
- **Não bloqueie a thread de rede.** Operações com HSM e Postgres passam por filas e worker threads dedicadas, que usam `std::jthread` e `std::stop_token`.
- **Seja defensivo na rede.** Assuma que o link com a bandeira vai cair. Todo timeout tem que gerar MTI 0420 no SAF para não causar perda financeira ao lojista.
- **ISO 8583 estrito.** Respeite bitmap primário e secundário, campos fixos, LLVAR/LLLVAR e codificação BCD/ASCII.
- **Logging com spdlog.** Use `LOG_DEBUG` para dumps hex de mensagens ISO e `LOG_ERROR` para falhas de infraestrutura ou de integridade criptográfica. Nunca logue o PAN completo, PIN Block em claro ou chaves. Use `Tokenizer::maskPan`.
- **Separação por módulo:**
  - `src/network`: sockets e sessões
  - `src/iso8583`: parser e builder
  - `src/mapper`: conversão JSON ↔ ISO
  - `src/switch`: roteamento, circuit breaker e gerenciamento de transações
  - `src/crypto` e `src/security`: HSM, PIN Block e tokenização
  - `src/database` e `src/persistence`: Postgres
  - `src/saf`: Store and Forward

## Fluxo de branches

`develop` → `hml` → `master`. Desenvolva a partir de `develop` e promova as mudanças por Pull Request.
