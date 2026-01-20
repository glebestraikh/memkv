# Результаты тестирования производительности Repa

### Repa (8 workers, порт 6380)

#### Тест 1:
```bash
redis-benchmark -p 6380 -a admin -t set,get -n 100000 -c 50 -q
```
**Результат:**
- SET: 133511.34 requests per second, p50=0.231 msec                    
- GET: 137741.05 requests per second, p50=0.223 msec

**Redis:**
-  SET: 157480.31 requests per second, p50=0.159 msec                    
-  GET: 160771.70 requests per second, p50=0.167 msec

#### Тест 2:
```bash
redis-benchmark -p 6380 -a admin -t set,get -n 100000 -c 200 -q
```
**Результат:**
- SET: 156006.25 requests per second, p50=0.703 msec                    
- GET: 158982.52 requests per second, p50=0.703 msec     

**Redis:**
- SET: 154559.50 requests per second, p50=0.623 msec                    
- GET: 156985.86 requests per second, p50=0.639 msec

#### Тест 3:
```bash
redis-benchmark -p 6380 -a admin -t set,get -n 100000 -c 50 -P 16 -q
```
**Результат:**
- SET: 327868.84 requests per second, p50=1.399 msec                    
- GET: 349650.34 requests per second, p50=1.271 msec

**Redis:**
- SET: 1 190 476.25 requests per second, p50=0.519 msec     
- GET: 1 694 915.25 requests per second, p50=0.383 msec


#### Тест 4:
```bash
redis-benchmark -p 6380 -a admin -t set,get -n 10000 -c 50 -d 1024 -q
```
**Результат:**
- SET: 101010.10 requests per second, p50=0.143 msec      
- GET: 99009.90 requests per second, p50=0.063 msec
- 
**Redis:**
- SET: 125000.00 requests per second, p50=0.167 msec      
- GET: 158730.16 requests per second, p50=0.167 msec
