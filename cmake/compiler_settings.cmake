# Este arquivo define as configurações do compilador para o projeto principal
# e para todas as dependências baixadas via FetchContent.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Adiciona a flag -std=c++20 para garantir que seja aplicada em todos os casos.
# A verificação do compilador evita problemas em plataformas não-GCC/Clang.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
endif()
