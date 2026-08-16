| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | -------- | -------- | -------- |

# Wi-Fi Station Example

(See the README.md file in the upper level 'examples' directory for more information about examples.)

This example shows how to use the Wi-Fi Station functionality of the Wi-Fi driver of ESP for connecting to an Access Point.

## How to use example

### Configure the project

Open the project configuration menu (`idf.py menuconfig`).

In the `Example Configuration` menu:

* Set the Wi-Fi configuration.
    * Set `WiFi SSID`.
    * Set `WiFi Password`.

Optional: If you need, change the other options according to your requirements.

### Build and Flash

Build the project and flash it to the board, then run the monitor tool to view the serial output:

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for all the steps to configure and use the ESP-IDF to build projects.

* [ESP-IDF Getting Started Guide on ESP32](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
* [ESP-IDF Getting Started Guide on ESP32-S2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)
* [ESP-IDF Getting Started Guide on ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/index.html)

## Example Output
Note that the output, in particular the order of the output, may vary depending on the environment.

Console output if station connects to AP successfully:
```
I (589) wifi station: ESP_WIFI_MODE_STA
I (599) wifi: wifi driver task: 3ffc08b4, prio:23, stack:3584, core=0
I (599) system_api: Base MAC address is not set, read default base MAC address from BLK0 of EFUSE
I (599) system_api: Base MAC address is not set, read default base MAC address from BLK0 of EFUSE
I (629) wifi: wifi firmware version: 2d94f02
I (629) wifi: config NVS flash: enabled
I (629) wifi: config nano formatting: disabled
I (629) wifi: Init dynamic tx buffer num: 32
I (629) wifi: Init data frame dynamic rx buffer num: 32
I (639) wifi: Init management frame dynamic rx buffer num: 32
I (639) wifi: Init management short buffer num: 32
I (649) wifi: Init static rx buffer size: 1600
I (649) wifi: Init static rx buffer num: 10
I (659) wifi: Init dynamic rx buffer num: 32
I (759) phy: phy_version: 4180, cb3948e, Sep 12 2019, 16:39:13, 0, 0
I (769) wifi: mode : sta (30:ae:a4:d9:bc:c4)
I (769) wifi station: wifi_init_sta finished.
I (889) wifi: new:<6,0>, old:<1,0>, ap:<255,255>, sta:<6,0>, prof:1
I (889) wifi: state: init -> auth (b0)
I (899) wifi: state: auth -> assoc (0)
I (909) wifi: state: assoc -> run (10)
I (939) wifi: connected with #!/bin/test, aid = 1, channel 6, BW20, bssid = ac:9e:17:7e:31:40
I (939) wifi: security type: 3, phy: bgn, rssi: -68
I (949) wifi: pm start, type: 1

I (1029) wifi: AP's beacon interval = 102400 us, DTIM period = 3
I (2089) esp_netif_handlers: sta ip: 192.168.77.89, mask: 255.255.255.0, gw: 192.168.77.1
I (2089) wifi station: got ip:192.168.77.89
I (2089) wifi station: connected to ap SSID:myssid password:mypassword
```

Console output if the station failed to connect to AP:
```
I (589) wifi station: ESP_WIFI_MODE_STA
I (599) wifi: wifi driver task: 3ffc08b4, prio:23, stack:3584, core=0
I (599) system_api: Base MAC address is not set, read default base MAC address from BLK0 of EFUSE
I (599) system_api: Base MAC address is not set, read default base MAC address from BLK0 of EFUSE
I (629) wifi: wifi firmware version: 2d94f02
I (629) wifi: config NVS flash: enabled
I (629) wifi: config nano formatting: disabled
I (629) wifi: Init dynamic tx buffer num: 32
I (629) wifi: Init data frame dynamic rx buffer num: 32
I (639) wifi: Init management frame dynamic rx buffer num: 32
I (639) wifi: Init management short buffer num: 32
I (649) wifi: Init static rx buffer size: 1600
I (649) wifi: Init static rx buffer num: 10
I (659) wifi: Init dynamic rx buffer num: 32
I (759) phy: phy_version: 4180, cb3948e, Sep 12 2019, 16:39:13, 0, 0
I (759) wifi: mode : sta (30:ae:a4:d9:bc:c4)
I (769) wifi station: wifi_init_sta finished.
I (889) wifi: new:<6,0>, old:<1,0>, ap:<255,255>, sta:<6,0>, prof:1
I (889) wifi: state: init -> auth (b0)
I (1889) wifi: state: auth -> init (200)
I (1889) wifi: new:<6,0>, old:<6,0>, ap:<255,255>, sta:<6,0>, prof:1
I (1889) wifi station: retry to connect to the AP
I (1899) wifi station: connect to the AP fail
I (3949) wifi station: retry to connect to the AP
I (3949) wifi station: connect to the AP fail
I (4069) wifi: new:<6,0>, old:<6,0>, ap:<255,255>, sta:<6,0>, prof:1
I (4069) wifi: state: init -> auth (b0)
I (5069) wifi: state: auth -> init (200)
I (5069) wifi: new:<6,0>, old:<6,0>, ap:<255,255>, sta:<6,0>, prof:1
I (5069) wifi station: retry to connect to the AP
I (5069) wifi station: connect to the AP fail
I (7129) wifi station: retry to connect to the AP
I (7129) wifi station: connect to the AP fail
I (7249) wifi: new:<6,0>, old:<6,0>, ap:<255,255>, sta:<6,0>, prof:1
I (7249) wifi: state: init -> auth (b0)
I (8249) wifi: state: auth -> init (200)
I (8249) wifi: new:<6,0>, old:<6,0>, ap:<255,255>, sta:<6,0>, prof:1
I (8249) wifi station: retry to connect to the AP
I (8249) wifi station: connect to the AP fail
I (10299) wifi station: connect to the AP fail
I (10299) wifi station: Failed to connect to SSID:myssid, password:mypassword
```

## Running the example on ESP Chips without Wi-Fi

This example can run on ESP Chips without Wi-Fi using ESP-Hosted. See the [Two-Chip Solution](../../README.md#wi-fi-examples-with-two-chip-solution) section in the upper level `README.md` for information.

## Troubleshooting

For any technical queries, please open an [issue](https://github.com/espressif/esp-idf/issues) on GitHub. We will get back to you soon.


Супутні налаштування для багатопоточностіДля забезпечення коректної роботи lwIP у багатопоточному середовищі переконайтеся, що також налаштовано пріоритет цього потоку:c#define TCPIP_THREAD_PRIO    (configMAX_PRIORITIES - 2)
Будьте обачні, використовуючи код.Примітка: Потік TCP/IP повинен мати вищий пріоритет, ніж потоки додатків, які викликають функції сокетів.

## Додатково встановити
idf.py add-dependency "espressif/esp32-camera"
idf.py add-dependency "espressif/cjson"
/CMakeLists.txt. О в списку REQUIRES espressif__cjson.

## ОБОВ'ЯЗКОВО
Через графічний інтерфейс VS Code (Найпростіший)Натисніть комбінацію клавіш Ctrl + Shift + P.Введіть та виберіть: ESP-IDF: SDK Configuration Editor (або натисніть на іконку шестірні на нижній панелі VS Code).У полі пошуку вгорі введіть: MBEDTLS_EXTERNAL_MEM_ALLOC або External SPIRAM.Поставте галочку навпроти цього пункту.Натисніть кнопку Save вгорі праворуч.

## Важливо
Де знайти налаштування довжини фрагментів у ESP-IDF v6Відкрийте idf.py menuconfig та перейдіть у:Component config ➡️ mbedTLS.Знайдіть і увімкніть опцію: Asymmetric in/out fragment length (CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN). Вона повинна мати значення [X] (Enabled).Після цього під нею з'являться два нових параметри:TLS maximum incoming fragment length (CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN) — залиште за замовчуванням 16384 (це критично для стабільного рукостискання TLS з серверами Telegram).TLS maximum outgoing fragment length (CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN) — за замовчуванням там зазвичай стоїть 4096.🚀 Що з цим зробити для прискорення?Ваше фото займає 9696 байт. Якщо вихідний (outgoing) буфер обмежений значенням 4096, mbedTLS змушений дробити ваш запит мінімум на 3 окремі зашифровані TLS-записи, відправляти їх по черзі мережею та чекати на підтвердження (ACK).Рішення: Збільшіть TLS maximum outgoing fragment length з 4096 до 16384 (або хоча б до 12288, щоб туди повністю влізло фото разом із HTTP-заголовками).

## Важливо

//Вимкнення Modem-sleep (прибирає мікрозасинання та пінг-спайки).
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    Рекомендований порядок викликів:esp_wifi_init(&cfg) — виділення ресурсів під драйвер.
    esp_wifi_set_mode(WIFI_MODE_STA) — встановлення режиму станції.
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config) — запис налаштувань мережі.
    esp_wifi_set_ps(WIFI_PS_NONE) — вимкнення Modem-sleep (прибирає мікрозасинання та пінг-спайки).
    esp_wifi_start() — запуск Wi-Fi підсистеми.
    esp_wifi_connect() — безпосередній запуск процесу авторизації в мережі.
    
    Також допускається перемикання режимів енергозбереження «на льоту» після отримання події IP_EVENT_STA_GOT_IP, проте застосування на етапі ініціалізації гарантує відсутність затримок (Latency) із самого початку з'єднання.

## Шифрування

* Для створення ключа.
    Ця команда миттєво створить у папці, де ви перебуваєте, файл ota_encryption_key.bin, який міститиме ідеальну послідовність випадкових байтів.
    Відкрийте термінал у вашому VS Code і виконайте одну коротку команду:    
    python -c "import os; open('ota_encryption_key.bin', 'wb').write(os.urandom(32))"
    Потім перенести в main та налаштувати cmake:
        idf_component_register(SRCS "main.c"                     # Ваші файли вихідного коду
                                INCLUDE_DIRS "."                  # Шляхи до папок з include
                                EMBED_BINARIES "ota_encryption_key.bin")  # <--- ДОДАЙТЕ ЦЕЙ РЯДОК

* Генерація приватного ключа підпису. Якщо не використовувати то галку перед Enable hardware Secure Boot in bootloader ставити не протрібно
    Відкрийте термінал ESP-IDF та виконайте команду залежно від вашого чипа:
        - Для ESP32-S3 / S2 / C3 / C6 / H2 (Рекомендовано ECDSA):
        espsecure.py generate_signing_key --version 2 --scheme ecdsa_p256 secure_boot_signing_key.pem
        - Для базового ESP32 (або якщо потрібен RSA-3072):
        espsecure.py generate_signing_key --version 2 --scheme rsa3072 secure_boot_signing_key.pem
  

    Для шифрування потрібно після компіляції виконати команду:

    espsecure.py encrypt_flash_data --keyfile main/ota_encryption_key.bin --address 0x0 --output station_enc.bin build/station.bin

*   Мій варіант
    Якщо ви не хочете вручную випалювати eFuse блоки на кожній платі за допомогою консолі, перейдіть на стандартний для цієї бібліотеки механізм RSA-3072. 
    Він налаштовується значно простіше і не потребує eFuse-команд.Крок 1. Перегенерація ключа в формат RSA (Замість .bin файлу).
    Відкрийте термінал і створіть пару ключів через OpenSSL:bash# Створюємо приватний RSA-ключ (Він залишається на ПК та вшивається в плату):
        openssl genrsa -out main/ota_private_key.pem 3072

    Витягуємо публічний ключ (Ним ви будете шифрувати файли перед завантаженням на GitHub)
        openssl rsa -in main/ota_private_key.pem -pubout -out main/ota_public_key.pem

    Шифрування файла перед передачею на GITHUB
    espsecure.py encrypt_flash_data --keyfile main/ota_public_key.pem --output station_enc.bin build/station.bin

*    Встановлення OpenSSL (якщо його немає)Найпростіший спосіб встановити OpenSSL через стандартний менеджер пакетів Windows (winget).
     Відкрийте PowerShell від імені Адміністратора.Виконайте команду:
        powershellwinget install XP8BT8QV1DB0C5 --source winget

        Змінні оточення... ДОДАТИ В path СТРОКУ C:\Program Files\Git\usr\bin