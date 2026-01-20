# CONFIG GET/SET - Документация

## Описание

Команды `CONFIG GET` и `CONFIG SET` позволяют просматривать и изменять параметры конфигурации сервера Repa во время его работы без перезапуска.

## Синтаксис

```
CONFIG GET <параметр>
CONFIG SET <параметр> <значение>
```

## Поддерживаемые параметры

### 1. Параметры памяти

#### `maxmemory`

- **Пример**:
  ```
  CONFIG SET maxmemory 536870912
  CONFIG GET maxmemory
  ```

#### `maxmemory-mb`
- **Пример**:
  ```
  CONFIG SET maxmemory-mb 512
  CONFIG GET maxmemory-mb
  ```

### 2. Параметры TTL

#### `default-ttl`
  ```
  CONFIG SET default-ttl 3600
  CONFIG GET default-ttl
  ```

### 3. Параметры производительности

#### `workers`
  ```
  CONFIG GET workers
  ```