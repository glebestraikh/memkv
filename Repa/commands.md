# Примеры использования команд Repa

## Подключение и аутентификация

```bash
./secondSemester/bin/repactl
# Введите Имя и Пароль: admin admin
```

## 1. HELLO - Приветствие сервера (протокол RESP2)

```
HELLO 2
```
**Ожидаемый ответ:** `OK`

## 2. AUTH - Аутентификация

```
AUTH admin admin
```
**Ожидаемый ответ:** `OK`

## 3. PING - Проверка соединения

```
PING
```
**Ожидаемый ответ:** `PONG`

## 4. SET - Установка значения ключа

Базовое использование:
```
SET mykey myvalue
```
**Ожидаемый ответ:** `OK`

Установка числового значения:
```
SET counter 42
```
**Ожидаемый ответ:** `OK`

Установка строки с пробелами (используйте кавычки в некоторых клиентах):
```
SET message "Hello World"
```
**Ожидаемый ответ:** `OK`

## 5. GET - Получение значения ключа

```
GET mykey
```
**Ожидаемый ответ:** `myvalue`

Получение несуществующего ключа:
```
GET nonexistent
```
**Ожидаемый ответ:** `(nil)`

## 6. DEL - Удаление ключей

Удаление одного ключа:
```
DEL mykey
```
**Ожидаемый ответ:** `(integer) 1` (если удален) или `(integer) 0` (если не найден)

Удаление нескольких ключей:
```
DEL key1 key2 key3
```
**Ожидаемый ответ:** `(integer) N` (количество удаленных ключей)

## 7. EXPIRE - Установка времени жизни ключа

Установка TTL 60 секунд:
```
SET tempkey tempvalue
EXPIRE tempkey 60
```
**Ожидаемый ответ:** `(integer) 1` (если ключ существует)

Для несуществующего ключа:
```
EXPIRE nonexistent 60
```
**Ожидаемый ответ:** `(integer) 0`

## 8. TTL - Получение времени жизни ключа

Проверка TTL:
```
SET testkey testvalue
EXPIRE testkey 300
TTL testkey
```
**Ожидаемый ответ:** `(integer) 300`

Для ключа без TTL:
```
SET persistkey persistvalue
TTL persistkey
```
**Ожидаемый ответ:** `(integer) -1`

Для несуществующего ключа:
```
TTL nonexistent
```
**Ожидаемый ответ:** `(integer) -2`

## 9. CONFIG GET - Получение параметров конфигурации

Получить максимальную память в байтах:
```
CONFIG GET maxmemory
```
**Ожидаемый ответ:** 
```
1) "maxmemory"
2) "268435456"
```

Получить максимальную память в мегабайтах:
```
CONFIG GET maxmemory-mb
```
**Ожидаемый ответ:**
```
1) "maxmemory-mb"
2) "256"
```

## 10. CONFIG SET - Установка параметров конфигурации

```
CONFIG SET maxmemory 536870912
```
**Ожидаемый ответ:** `OK`

## 11. STATS - Получение статистики сервера

```
STATS
```
**Ожидаемый ответ:**
```
STATS
1. Requests
  total_commands_processed   123
  cmd_get                    45
  cmd_set                    38
  cmd_del                    12
  cmd_ping                   5
  cmd_auth                   3
  cmd_config                 2
  cmd_expire                 8
  cmd_ttl                    5
  cmd_stats                  1
  cmd_other                  4

2. Cache
  cache_hits                 40
  cache_misses               5
  hit_ratio                  88.9%

3. Memory
  used_memory_bytes          1024  (0.0 / 256.0 MiB, 0.0%)

4. Connections / Uptime
  current_connections        1
  total_connections_received 10
  uptime_s                   3600  (1h 0m 0s)
```

## 12. QUIT - Закрытие соединения

```
QUIT
```
**Ожидаемый ответ:** `OK`, затем соединение закрывается

