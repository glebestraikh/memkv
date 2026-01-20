# Реализация In-memory Key-Value хранилищ на языке C с поддержкой протокола RESP2

Проект включает 2 варианта реализации:

## Структура проекта

- **Repa/**  
  In-memory key/value store с использованием `pthread`.

- **Coro/**  
  Собственные корутины на C.

- **RepaCoro/**  
  In-memory key/value store, работающий на собственных корутинах.
