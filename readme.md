# Автоматическая поливка растений
На базе датчиков влажности почвы и ардуино сделать систему, которая поливает комнатные растения. С интеграцией с Алисой.

Текущее состояние. Освоена IDE, нарисован прототип экрана, сформулирован список компонентов и функционал.

## Использование
Используйте на свой страх и риск. Автор не компетентен в электронике.

## Начало работы
- Для работы используется связка [VsCode](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/platformio-ide). Деплой и компиляция осуществляется средствами IDE.
- В библиотеке SdFat подправить константу SPI_DRIVER_SELECT -> 2 (не смог победить на одной SPI шине SD карту и экран) libs/../SdFat/src/SdFatConfig.h 
- Cоздать SecretHolder.cpp и реализовать там методы !TODO!

### Зачем вы разработали этот проект?
Чтобы был.

## Команда проекта
- tg m9ist

## Компоненты

Ардуино
Mega2560+WiFi-R3-AT328-ESP8266-32MB-CH340G

дисплей
IPS TFT RGB дисплей 1.3" дюйма, 240х240 px, на базе ST7789 цветной
модель ZJY-IPS130-V2.0
схема правильного включения https://simple-circuit.com/wp-content/uploads/2019/06/arduino-st7789-color-tft-240x240-pixel-interfacing-circuit.png

Модуль часов реального времени
модель DS1302
чип DS1307
https://www.nookery.ru/ds1302-in-arduino/

насос
модель R385

драйвера шагового двигателя для насоса
модель MX1508
чип L298N
https://robotchip.ru/obzor-drayvera-motora-na-l298n/

драйвер шагового двигателя для клапанов
WAVGAT ULN2003 драйвер шагового двигателя 5V 4 фазы
model ULN2003

Датчик температуры и влажности
модель GY-SHT30-D
чип SHT30

SD ридер
модель hw-125
чип B108

электромагнитный клапан
dc5v 20190526 HUXUAN 5В

датчик расхода воды
HESAI Water Flow Sensor
модель YF-S401
[дока](https://wiki.iarduino.ru/page/sensor-water-flow/)


Кандидат в датчик влажности почвы. Аналог.
модель FC-28
https://espsmart.ru/blog/15-podkljuchenie-datchika-vlazhnosti-fc-28-k-esp8266.html

Кандидат в датчик влажности почвы TZT. Аналог.
модель YL-69 (?) - то же самое
https://3d-diy.ru/blog/datchik-vlazhnosti-pochvy-arduino/?srsltid=AfmBOopTGEUquAPRKpB6Ttx5A9FN63evuNqrRIelx4QxLuqCEawt0BYT
электрическая схема https://components101.com/modules/soil-moisture-sensor-module (просто 10к резистор на питании)

Кандидат в датчик влажности почвы SZYTF Soil Moisture Sensor. Аналог.
модель HD-38
Датчик временной рефлектометрии TDR
https://github.com/vrxfile/test_arduino_sensors_modules/blob/master/capacitive_moisture_test/capacitive_moisture_test.ino
https://myduino.com/product/jhs-273/

Кандидат в датчик влажности почвы TENSTAR ROBOT Capacitive. Аналог. v1.2
Емкостной датчик влажности почвы.
Если не давать постоянно напряжение, то нужно 50ms дать на "разогрев" https://wiki.iarduino.ru/page/capacitive-soil-moisture-sensor/
Датчик возвращает инверсное значение: чем больше влажность, тем ниже показания. Максимальные показания  - датчик находится в воздухе, минимальные - датчик находится в воде по линию пиктограммы деревьев (65мм). 

Датчик уровня воды
модель XKC-Y25-NPN 5-12v
https://wiki.amperka.ru/products:sensor-liquid-level-contactless

Тумблеры
Пищалка
Датчик освещенности
Кнопки
Предохранители

Подсчет выходов:
|  N | элемент                  | тип | Кол-во    |
|----|--------------------------|-----|-----------|
|  1 | Экран                    | d   | 5         |
|  2 | Модуль реального времени | d   | 3         |
|  3 | Пищалка                  | d   | 1         |
|  4 | Датчик влаж и темп возд  |     |           |++
|  5 | SD карта                 | d   | 4         |
|  6 | Датчик расхода воды      |     |           |
|  7 | Датчик уровня воды       | d   | 1         |
|  8 | Управление помпой        | d   | 2 (1?)    |
|  9 | Тумблер отключения двиг  | d   | 1         |
| 10 | Кнопка режима экрана     | d   | 3         |
| 11 | Датчик освещенности      |     |           |
| 12 | Кнопка проверки          | d   | 1         |
Итого: 23-26 d

Одно растение
|  N | элемент                          | тип | Кол-во |
|----|----------------------------------|-----|--------|
|  1 | Кнопка принудительного полива    | d   | 1      |
|  2 | Датчик влажности почвы питание   | d   | 1      |
|  3 | Датчик влажности почвы показания | a   | 1      |
|  4 | Управление клапаном              | d   | 1      |
|  5 | Тумблер растения                 | d   | 1      |
Итого: 4d + 1a

# Интеграция с Алисой:
Инструкция от яндекса https://yandex.cloud/ru/docs/iot-core/quickstart
Сертификат
openssl req -x509 -newkey rsa:4096 -keyout private-key.pem -out cert.pem -nodes -days 365 -subj "/CN=localhost"
Важно, сгенерировать сертификат как для реестра, так и для устройства.

Инструкция как получать токены для приложения https://yandex.cloud/ru/docs/iam/operations/iam-token/create#via-cli
- получить OAuth токен
- сделать пост запрос (советуют раз в час) `curl --request POST --data "{\"yandexPassportOauthToken\":\"OAthToken\"}" https://iam.api.cloud.yandex.net/iam/v1/tokens`